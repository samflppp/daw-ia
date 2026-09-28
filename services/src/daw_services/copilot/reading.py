"""The generation window's prompt, read by the copilot's model (S16).

One language: the words typed in the window go through the same model, with the
same translation rules, as a request typed in the copilot panel. What comes back
is not notes and not commands: it is an interpretation in the contract of S14
(the fields of pattern.generate, plus the words that could not be used). The DAW
draws the notes itself, with its own generator.

The schema is not written here. It is pattern.generate's, as the DAW serves it,
without the fields that say where to write: the window already knows where. A
field added to the contract on the C++ side reaches this reader without a line
changed here.
"""

from __future__ import annotations

import json
from typing import Any

from daw_services.ia_provider import IAProvider, ProviderUnavailable, Usage
from daw_services.rpc import Peer, RpcError

READ_TOOL = "prompt.read"
GENERATE_TOOL = "pattern.generate"

# Where to write is the window's business, not the prompt's.
PLACEMENT_FIELDS = ("clipId", "fromBeats", "toBeats", "variant")

# How notes already in the zone are reworked (S16). The names are the DAW's
# (generation/Transform.h).
TRANSFORMS = {
    "keep_rhythm": "garder le rythme, changer les hauteurs",
    "keep_pitches": "garder les hauteurs, changer le rythme",
    "variation": "une variante légère, comme un A' : une seule note change",
    "darker": "plus sombre : même rythme, notes assombries",
    "brighter": "plus clair : même rythme, notes éclaircies",
    "busier": "plus rythmé : mêmes notes, plus d'attaques",
    "calmer": "plus calme, plus simple : mêmes notes, moins d'attaques",
    "humanize": "humaniser : mêmes notes, placements et vélocités moins carrés",
}

# Short answer, little thinking: this is a word-to-field mapping, and the
# window waits for it.
READ_MAX_TOKENS = 1024
READ_EFFORT = "low"

TRANSLATION_RULES = """Traduis l'intention en contraintes :
  « triste », « sombre », « mélancolique » donnent une tonalité mineure ;
  « joyeux », « lumineux » une tonalité majeure ;
  « calme », « posé », « aéré » une densité sparse ; « énergique », « chargé » dense ;
  « grave » le registre low, « aigu » high ;
  « accords », « nappe » le rôle chords ; « basse », « 808 » bass ;
  « mélodie », « lead », « topline » melody ; « batterie », « hats », « rythme » rhythm ;
  « boucle », « loop » la forme loop ; « qui varie » varied.
  « trap », « drill » : doubles-croches (1/16) ; « lofi », « boom-bap » : croches (1/8)."""

READING_PROMPT = f"""Tu lis ce qu'une personne a écrit dans la fenêtre de génération d'un logiciel
de musique, pour une zone qu'elle a choisie. Le générateur du logiciel écrira les notes ;
toi, tu dis seulement ce qu'elle demande, en contraintes.

Appelle {READ_TOOL} une seule fois, sans rien écrire d'autre.
- Ne remplis un champ que si la demande le dit ou l'implique clairement. Un champ omis
  est déduit des notes autour de la zone : c'est souvent le mieux.
- Si la zone contient déjà des notes (hasNotes), la demande les retravaille : dis
  comment dans transform. Sans consigne claire, keep_rhythm. Une tonalité ou un
  registre demandés en plus se mettent dans leurs champs.
- Mets dans ignored les mots de la demande qui portent une intention musicale que
  tu n'as pu traduire en aucun champ. N'y mets pas les mots vides (« fais-moi », « des »).

{TRANSLATION_RULES}"""


def reading_tool(generate: dict[str, Any]) -> dict[str, Any]:
    """prompt.read: pattern.generate's constraints, and the words left over."""
    schema = dict(generate.get("input_schema") or {})
    properties = {
        name: value
        for name, value in dict(schema.get("properties") or {}).items()
        if name not in PLACEMENT_FIELDS
    }
    properties["transform"] = {
        "type": "string",
        "enum": list(TRANSFORMS),
        "description": "Seulement si la zone contient des notes : comment les retravailler. "
        + " ; ".join(f"{name} = {meaning}" for name, meaning in TRANSFORMS.items()),
    }
    properties["ignored"] = {
        "type": "array",
        "items": {"type": "string"},
        "description": "Les mots d'intention musicale que tu n'as pu traduire en aucun champ.",
    }
    return {
        "name": READ_TOOL,
        "description": "Rend la demande de la personne en contraintes pour le générateur du DAW.",
        "input_schema": {
            "type": "object",
            "properties": properties,
            "additionalProperties": False,
        },
    }


class Reader:
    """Reads prompts. Holds the DAW's schema once it has asked for it, and nothing else."""

    def __init__(self, daw: Peer, provider: IAProvider) -> None:
        self._daw = daw
        self._provider = provider
        self._tool: dict[str, Any] | None = None

    def read(self, text: str, zone: dict[str, Any]) -> dict[str, Any]:
        """Returns {"interpretation": {...}, "usage": {...}} or {"failed": True, "message": ...}."""
        try:
            tool = self._reading_tool()
        except RpcError as failure:
            return {"failed": True, "message": f"Le DAW ne répond pas : {failure}"}
        if tool is None:
            return {"failed": True, "message": f"Le DAW ne sert pas {GENERATE_TOOL}."}

        messages = [
            {
                "role": "user",
                "content": (
                    f"Zone : {json.dumps(zone, ensure_ascii=False, separators=(',', ':'))}\n\n"
                    f"Demande : {text}"
                ),
            }
        ]
        try:
            turn = self._provider.converse(
                READING_PROMPT, messages, [tool], max_tokens=READ_MAX_TOKENS, effort=READ_EFFORT
            )
        except ProviderUnavailable as failure:
            return {"failed": True, "message": str(failure)}

        for call in turn.tool_calls:
            if call.name == READ_TOOL and turn.stop_reason != "max_tokens":
                read: dict[str, Any] = {
                    "interpretation": interpretation_of(call.arguments),
                    "usage": turn.usage.as_dict(),
                }
                transform = call.arguments.get("transform")
                if zone.get("hasNotes") and transform in TRANSFORMS:
                    read["transform"] = transform
                return read

        return {
            "failed": True,
            "message": turn.text.strip() or "Le modèle n'a pas lu la demande.",
            "usage": turn.usage.as_dict(),
        }

    def _reading_tool(self) -> dict[str, Any] | None:
        if self._tool is None:
            tools = self._daw.request("tools.list") or []
            generate = next((tool for tool in tools if tool.get("name") == GENERATE_TOOL), None)
            if generate is None:
                return None
            self._tool = reading_tool(generate)
        return self._tool


def interpretation_of(arguments: dict[str, Any]) -> dict[str, Any]:
    """The S14 Interpretation shape: the constraint fields, ignored, conflicts."""
    out = {
        name: value
        for name, value in arguments.items()
        if name not in (*PLACEMENT_FIELDS, "ignored", "transform")
    }
    ignored = arguments.get("ignored") or []
    out["ignored"] = [str(word) for word in ignored if str(word).strip()]
    out["conflicts"] = []
    return out


__all__ = [
    "READING_PROMPT",
    "READ_TOOL",
    "TRANSFORMS",
    "Reader",
    "Usage",
    "interpretation_of",
    "reading_tool",
]
