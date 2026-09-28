"""The copilot: one request typed in French becomes one history entry in the DAW.

What it is not: it holds no state of the project, it invents no command type, it
has no privilege. It reads what the DAW shows, calls the tools the DAW declares,
and the DAW refuses a bad payload from it exactly as it would from a button.

The turn, in order:

  1. the model is given the project summary, the tools, and the request
  2. its read calls are answered at once, because reading changes nothing
  3. its command calls are staged, never applied one by one
  4. after each turn, the DAW tries what is staged on a copy of the project
     (commands.check); a call it refuses is taken back out and the model is
     told why, so it can correct it in the same request
  5. when it stops talking, the staged commands go to the DAW as one group

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

from daw_services.copilot.reading import TRANSLATION_RULES, Reader
from daw_services.ia_provider import (
    DEFAULT_MODEL,
    AnthropicProvider,
    IAProvider,
    ProviderUnavailable,
    ToolCall,
    Usage,
)
from daw_services.rpc import Peer, RpcError

MAX_TURNS = 8
NEW_ID_PATTERN = re.compile(r"^\$new:[a-z0-9_-]{1,32}$")
CROCKFORD = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"

SYSTEM_PROMPT = f"""Tu pilotes un logiciel de musique. Tu réponds en français, brièvement.

Ce que tu peux faire :
- lire l'état du projet, les notes d'un clip, les plugins installés ;
- mesurer les niveaux de chaque piste et du master (mix.get_levels) ;
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
- La playlist a des lignes libres, comme FL : on y range les blocs, elles ne
  sonnent pas. Une pose sans laneId est sur la ligne de son pattern, dont
  l'identifiant est celui du pattern. lane.create, lane.rename, lane.move et
  lane.remove éditent les lignes ; placement.move et audio.move prennent un
  laneId pour changer un bloc de ligne. Ranger un projet, c'est nommer des
  lignes et y déplacer des blocs, jamais changer une note.
- Les positions des outils et de l'état sont en temps (beats), à 4 temps par
  mesure. À l'utilisateur, parle en mesures, comptées depuis 1 : le temps t
  est la mesure t / 4 + 1. Le temps 128 est la mesure 33, jamais « la mesure
  128 ». Utilise « temps » seulement pour une position qui tombe dans une
  mesure.
- Pour écrire de la musique (des accords, une mélodie, une basse, un rythme),
  appelle pattern.generate sur la ligne où elle doit aller, une fois par ligne.
  Le générateur du DAW écrit des notes justes, répétées selon une forme, dans
  le style appris de l'utilisateur. Dis dans ta phrase finale ce que tu as
  choisi. N'utilise note.add que pour des notes que l'utilisateur dicte une à
  une.
- {TRANSLATION_RULES}
- La ligne doit exister avant pattern.generate : crée-la dans la même requête
  (clip.create_midi, ou pattern.add_track) et donne son $new: comme clipId.
- Le générateur change d'accord à chaque mesure : sur une mesure, il n'y a
  qu'un accord. Des accords, une basse ou une mélodie se demandent sur une
  progression : quatre mesures (16 temps en 4/4) sauf si l'utilisateur dit
  une autre longueur. Un clip.create_midi pour cela a lengthBeats 16 ; un
  pattern existant plus court s'allonge d'abord avec pattern.set_length, dans
  la même requête. Ne raccourcis jamais un pattern pour cela.
- Un instrument se charge avec track.add puis plugin.insert sur cette piste ;
  cherche d'abord le plugin (plugins.search) pour son identifiant exact.
- Si un appel est refusé par le DAW, le résultat de l'outil le dit : corrige
  cet appel et renvoie-le. Les autres restent en attente.
- track.set_channel_pitch règle la hauteur d'un canal dans le channel rack. Il
  ne change aucune note déjà écrite.
- Pour quantifier ou transposer, lis d'abord les notes du clip : il faut leurs
  identifiants.
- Le mixage : chaque piste va au master, ou dans un bus (track.set_output). Un
  bus est une tranche sans contenu, créée par bus.add ; les envois
  (track.set_send) en prennent une partie après le fader, pour une réverbe
  partagée par exemple. Les bus et le master se règlent avec les commandes des
  pistes (volume, pan, coupure, plugins), en donnant leur identifiant comme
  trackId : busId pour un bus, master.trackId pour le master. track.set_solo
  met en solo, sans toucher à la coupure.
- Les niveaux sont mesurés sur ce qui sonne, pendant les 300 dernières
  millisecondes : à l'arrêt, tout est à -100 dB. Ce sont des dBFS, crête et
  RMS ; over dit qu'une crête a atteint 0 dBFS. Ne déduis jamais un niveau
  d'un volume : mesure-le.
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

READ_TOOLS.append(
    {
        "name": "mix.get_levels",
        "description": (
            "Mesure ce qui sonne maintenant : crête et RMS en dBFS, gauche, droite et ensemble, "
            "pour chaque piste et pour le master, sur les 300 dernières millisecondes. "
            "over indique une crête à 0 dBFS ou plus."
        ),
        "input_schema": {"type": "object", "properties": {}, "additionalProperties": False},
    }
)

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
        origins: list[str] = []  # the call each staged command came from
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

            # A turn stopped by the length limit ends in a call whose arguments
            # were cut: it is not staged, and the model is told to send it again.
            cut = turn.stop_reason == "max_tokens"
            results = []
            for rank, call in enumerate(turn.tool_calls):
                if cut and rank == len(turn.tool_calls) - 1:
                    results.append(_error(call, CUT_CALL))
                    continue
                results.append(self._run(call, staged, minted, origins))

            self._check(staged, origins, results)
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

    def _run(
        self,
        call: ToolCall,
        staged: list[dict[str, Any]],
        minted: dict[str, str],
        origins: list[str],
    ) -> dict[str, Any]:
        if call.name in READ_TOOL_NAMES:
            return _result(call, self._read(call))

        # A command: staged, not applied. The DAW applies the lot at the end,
        # in one group, or applies none of it.
        staged.append({"type": call.name, "payload": resolve_new_ids(call.arguments, minted)})
        origins.append(call.call_id)
        return _result(call, {"staged": True})

    def _check(self, staged: list[dict[str, Any]], origins: list[str], results: list[dict[str, Any]]) -> None:
        """Tries what is staged on a copy of the project, and takes back what it refuses.

        Each refusal removes one command and tells the model which call it was
        and why, in that call's own result when it is from this turn. A DAW
        that does not know commands.check is not asked again: the group is
        then judged when it is applied, as before.
        """
        while staged:
            try:
                verdict = self._daw.request("commands.check", {"commands": staged}, timeout=30.0)
            except RpcError:
                return
            if not isinstance(verdict, dict) or verdict.get("ok", True):
                return

            index = int(verdict.get("index", -1))
            if not 0 <= index < len(staged):
                return

            call_id = origins.pop(index)
            staged.pop(index)
            said = (
                f"Refusé par le DAW, rien n'est appliqué : {verdict.get('message', '')}. "
                "Corrige cet appel et renvoie-le."
            )
            for rank, result in enumerate(results):
                if result.get("tool_use_id") == call_id:
                    results[rank] = {**result, "content": said, "is_error": True}
                    break
            else:
                results.append({"type": "text", "text": said})

    def _read(self, call: ToolCall) -> dict[str, Any]:
        try:
            if call.name == "project.get_state":
                return {"state": self._daw.request("state.get")}

            if call.name == "mix.get_levels":
                return self._daw.request("mix.levels")

            if call.name == "clip.get_notes":
                return self._daw.request("clip.notes", {"clipId": call.arguments.get("clipId", "")})

            return self._daw.request("plugins.find", {"query": call.arguments.get("query", "")})
        except RpcError as failure:
            return {"error": str(failure)}


CUT_CALL = (
    "Cet appel a été coupé par la limite de longueur de la réponse et n'a pas été pris. "
    "Renvoie-le entier, et écris moins d'appels : pattern.generate écrit une ligne entière en un seul."
)


def _error(call: ToolCall, message: str) -> dict[str, Any]:
    return {"type": "tool_result", "tool_use_id": call.call_id, "content": message, "is_error": True}


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

    reader = Reader(peer, chosen)

    def interpret(params: dict[str, Any]) -> dict[str, Any]:
        text = str(params.get("text", "")).strip()
        zone = params.get("zone") if isinstance(params.get("zone"), dict) else {}
        if not text:
            return {"failed": True, "message": "Rien à lire."}
        read = reader.read(text, zone)
        usage = read.get("usage") or {}
        print(
            f"lecture : {usage.get('inputTokens', 0)} jetons en entrée, "
            f"{usage.get('outputTokens', 0)} en sortie",
            flush=True,
        )
        return read

    peer.on("copilot.ask", ask)
    peer.on("generation.interpret", interpret)
    peer.serve_forever()
    return 0
