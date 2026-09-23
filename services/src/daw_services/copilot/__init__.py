"""The copilot: one request typed in French becomes one history entry in the DAW.

What it is not: it holds no state of the project, it invents no command type, it
has no privilege. It reads what the DAW shows, calls the tools the DAW declares,
and the DAW refuses a bad payload from it exactly as it would from a button.

The turn, in order:

  1. the model is given the project summary, the tools, and the request
  2. its read calls are answered at once, because reading changes nothing
  3. its command calls are staged, never applied one by one
  4. when it stops talking, the staged commands go to the DAW as one group

Step 3 is the whole point. A command applied the moment the model asked for it
would leave a project half changed the day the second command is refused, and
"add a Bass track and put Vital on it" would leave a Bass track with nothing on
it. One group, applied by the DAW, all or nothing.
"""

from __future__ import annotations

import json
import os
import re
import secrets
import time
from dataclasses import dataclass, field
from typing import Any

from daw_services.ia_provider import (
    DEFAULT_MODEL,
    AnthropicProvider,
    IAProvider,
    ProviderUnavailable,
    ToolCall,
    Usage,
)
from daw_services.rpc import Peer, RpcError

MAX_TURNS = 6
NEW_ID_PATTERN = re.compile(r"^\$new:[a-z0-9_-]{1,32}$")
CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

SYSTEM_PROMPT = """Tu pilotes un logiciel de musique. Tu réponds en français, brièvement.

Ce que tu peux faire :
- lire l'état du projet, les notes d'un clip, les plugins installés ;
- demander des modifications en appelant les outils de commande.

Règles :
- Une requête de l'utilisateur donne une seule action dans son historique.
  Appelle tous les outils nécessaires dans le même tour.
- Les volumes sont absolus : pour « monte de 3 dB », lis le volume actuel et
  envoie la somme.
- Pour créer quelque chose, donne un identifiant de la forme $new:nom. Le même
  nom dans la même requête désigne la même chose.
- Le contenu vit dans des patterns. Un pattern porte une ligne par piste et
  une longueur ; un placement dit où ce pattern se joue. Une note s'écrit dans
  une ligne, jamais sur une piste directement.
- Écrire sur une seule piste : clip.create_midi fait le pattern, la ligne et
  le placement d'un coup, et rend le clipId que note.add attend. Sur plusieurs
  pistes : pattern.create, pattern.place, puis un pattern.add_track par piste.
- Écrire dans un pattern qui existe déjà : prends le clipId de la ligne de
  cette piste dans l'état. Si elle n'y est pas, ouvre-la avec pattern.add_track.
- Un pattern posé huit fois se modifie une seule fois : les notes sont dans le
  pattern, jamais dans les placements. N'écris jamais la même note plusieurs
  fois pour couvrir plusieurs placements.
- Les patterns sont numérotés par leur rang (champ rank) : « le pattern 2 »
  est celui de rang 2, et label est le nom que l'utilisateur voit.
- La playlist est faite de placements. Répéter un pattern N fois, c'est N
  pattern.place du même pattern, bout à bout : le k-ième commence à
  début + k × lengthBeats. Ne crée jamais un nouveau pattern pour répéter.
  « À la suite » commence à arrangementEndBeats. placement.move déplace une
  pose, placement.remove en retire une ; aucune des deux ne touche aux notes.
- track.set_channel_pitch règle la hauteur d'un canal dans le channel rack. Il
  ne change aucune note déjà écrite.
- Pour quantifier ou transposer, lis d'abord les notes du clip : il faut leurs
  identifiants.
- Si la demande est ambiguë ou hors de ta portée, dis-le en une phrase et
  n'appelle aucun outil de commande. Ne devine pas.
- Termine par une phrase courte qui dit ce que tu as fait."""


def new_ulid() -> str:
    """A ULID, the way the DAW writes them: 48 bits of time, 80 random bits."""
    value = (int(time.time() * 1000) << 80) | secrets.randbits(80)
    text = ""
    for _ in range(26):
        text = CROCKFORD[value & 0x1F] + text
        value >>= 5

    return text


def resolve_new_ids(payload: Any, minted: dict[str, str]) -> Any:
    """Replaces every $new:name by a real identifier, the same one for one name.

    The rule the DAW keeps does not move: the caller supplies the identifier of
    what it creates. This is the caller, writing it. A model asked to invent
    twenty-six characters of Crockford base32 writes payloads the domain
    refuses, and there is nothing to learn from that refusal.
    """
    if isinstance(payload, dict):
        return {key: resolve_new_ids(value, minted) for key, value in payload.items()}

    if isinstance(payload, list):
        return [resolve_new_ids(item, minted) for item in payload]

    if isinstance(payload, str) and NEW_ID_PATTERN.match(payload):
        return minted.setdefault(payload, new_ulid())

    return payload


# --- the tools that only read ----------------------------------------------

READ_TOOLS: list[dict[str, Any]] = [
    {
        "name": "project.get_state",
        "description": (
            "Relit l'état du projet : tempo, pistes, patterns, lignes et placements, plugins, transport."
        ),
        "input_schema": {"type": "object", "properties": {}, "additionalProperties": False},
    },
    {
        "name": "clip.get_notes",
        "description": (
            "Les notes d'un clip, avec leurs identifiants. Nécessaire avant de quantifier ou de transposer."
        ),
        "input_schema": {
            "type": "object",
            "properties": {"clipId": {"type": "string", "description": "Identifiant du clip."}},
            "required": ["clipId"],
            "additionalProperties": False,
        },
    },
    {
        "name": "plugins.search",
        "description": "Cherche un plugin installé par son nom, quand la liste de l'état est coupée.",
        "input_schema": {
            "type": "object",
            "properties": {"query": {"type": "string", "description": "Un morceau du nom."}},
            "required": ["query"],
            "additionalProperties": False,
        },
    },
]

READ_TOOL_NAMES = {tool["name"] for tool in READ_TOOLS}


@dataclass
class Answer:
    """What one request produced."""

    text: str
    usage: Usage = field(default_factory=Usage)
    group_id: str = ""
    command_ids: list[str] = field(default_factory=list)
    failed: bool = False

    def as_dict(self) -> dict[str, Any]:
        return {
            "text": self.text,
            "usage": self.usage.as_dict(),
            "groupId": self.group_id,
            "commandIds": self.command_ids,
            "failed": self.failed,
        }


class Agent:
    """One request in, one answer out. Holds no project state of its own."""

    def __init__(self, daw: Peer, provider: IAProvider) -> None:
        self._daw = daw
        self._provider = provider

    def answer(self, request: str) -> Answer:
        try:
            state = self._daw.request("state.get")
            command_tools = self._daw.request("tools.list")
        except RpcError as failure:
            return Answer(text=f"Le DAW ne répond pas : {failure}", failed=True)

        tools = [*READ_TOOLS, *command_tools]
        messages: list[dict[str, Any]] = [
            {
                "role": "user",
                "content": (f"État du projet :\n{_compact(state)}\n\nDemande de l'utilisateur : {request}"),
            }
        ]

        staged: list[dict[str, Any]] = []
        minted: dict[str, str] = {}
        total = Usage()
        said = ""

        for _ in range(MAX_TURNS):
            try:
                turn = self._provider.converse(SYSTEM_PROMPT, messages, tools)
            except ProviderUnavailable as failure:
                return Answer(text=str(failure), usage=total, failed=True)

            total = total.plus(turn.usage)
            said = turn.text or said

            if not turn.tool_calls:
                break

            messages.append({"role": "assistant", "content": turn.raw_content})
            results = [self._run(call, staged, minted) for call in turn.tool_calls]
            messages.append({"role": "user", "content": results})

        if not staged:
            # A request the model would not act on is an answer, not a failure:
            # it said why, in French, and nothing was touched.
            return Answer(text=said or "Je n'ai rien fait.", usage=total)

        try:
            outcome = self._daw.request(
                "commands.execute",
                {"label": request[:120], "commands": staged},
                timeout=30.0,
            )
        except RpcError as failure:
            return Answer(text=f"Le projet a refusé : {failure}", usage=total, failed=True)

        return Answer(
            text=said or "C'est fait.",
            usage=total,
            group_id=str((outcome or {}).get("groupId", "")),
            command_ids=list((outcome or {}).get("commandIds", [])),
        )

    def _run(self, call: ToolCall, staged: list[dict[str, Any]], minted: dict[str, str]) -> dict[str, Any]:
        if call.name in READ_TOOL_NAMES:
            return _result(call, self._read(call))

        # A command: staged, not applied. The DAW applies the lot at the end,
        # in one group, or applies none of it.
        staged.append({"type": call.name, "payload": resolve_new_ids(call.arguments, minted)})
        return _result(call, {"staged": True})

    def _read(self, call: ToolCall) -> dict[str, Any]:
        try:
            if call.name == "project.get_state":
                return {"state": self._daw.request("state.get")}

            if call.name == "clip.get_notes":
                return self._daw.request("clip.notes", {"clipId": call.arguments.get("clipId", "")})

            return self._daw.request("plugins.find", {"query": call.arguments.get("query", "")})
        except RpcError as failure:
            return {"error": str(failure)}


def _result(call: ToolCall, content: Any) -> dict[str, Any]:
    return {
        "type": "tool_result",
        "tool_use_id": call.call_id,
        "content": _compact(content),
    }


def _compact(payload: Any) -> str:
    return json.dumps(payload, ensure_ascii=False, separators=(",", ":"))


def run(port: int, provider: IAProvider | None = None) -> int:
    """Connects to the DAW on `port` and answers until the link closes."""
    model = os.environ.get("DAW_IA_MODEL", "").strip() or DEFAULT_MODEL
    chosen = provider or AnthropicProvider(model)
    peer = Peer.connect(port)
    agent = Agent(peer, chosen)

    def ask(params: dict[str, Any]) -> dict[str, Any]:
        request = str(params.get("request", "")).strip()
        if not request:
            return Answer(text="Rien à faire.", failed=True).as_dict()

        answer = agent.answer(request)
        print(
            f"copilot: {answer.usage.input_tokens} jetons en entrée, {answer.usage.output_tokens} en sortie",
            flush=True,
        )
        return answer.as_dict()

    peer.on("copilot.ask", ask)
    peer.serve_forever()
    return 0
