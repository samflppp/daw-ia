"""The mix decided by the model (S20): numbers in, a proposal out.

Measuring is not deciding. The DAW measures — loudness, crest, octaves, width,
which tracks mask which — and sends those numbers, the role of each track, its
chain, the axes and the bounds. The model never hears a sample of audio. It
answers with one call of mix.propose: target values for existing settings
(volume, pan, the DAW's equaliser and compressor), each with a plain French
sentence that cites the measure behind it.

The DAW checks every change against its bounds, and every cited number against
the measure, before anything is tried. A refused change comes back here with
its reason; the model is told, once, and may correct it. Whatever it still
gets wrong is left out of the proposal, never applied.
"""

from __future__ import annotations

import json
from typing import Any

from daw_services.ia_provider import IAProvider, ProviderUnavailable

PROPOSE_TOOL = "mix.propose"
MIX_MAX_TOKENS = 12000
MIX_EFFORT = "medium"

MIXING_PROMPT = """Tu mixes un morceau dans un logiciel de musique. Tu ne l'entends pas : tu lis
ses mesures, piste par piste, et tu décides des réglages. Le logiciel les vérifie,
les essaie, et la personne les écoute avant de les garder.

Ce que tu reçois (JSON) :
- tracks : chaque piste, son rôle (role, roleKnown), son fader (volumeDb), son pan,
  sa chaîne (chain), son égaliseur et son compresseur du logiciel s'ils existent, et
  ses mesures avant fader : lufs (intégré), lufsShortMax, truePeak, crest (facteur de
  crête sur 10 ms, dB), bands (dix octaves, dBFS, centres dans bandsHz), correlation,
  side, active (part du temps où elle joue), peak10ms.
- master : les mêmes mesures sur ce qui sort.
- masking : les paires qui se masquent, l'octave (index dans bandsHz), share (% du
  temps où les deux jouent), levelDb, et les secondes où ça arrive. Jamais un kick
  avec une basse : un kick décroît et traverse la basse à chaque coup, le recouvrement
  ne dit rien d'eux.
- kickMargins : pour un kick et une basse, de combien le kick passe la basse sur ses
  coups (marginDb, dB, négatif s'il reste dessous), dans l'octave qui porte le kick
  (band, index dans bandsHz).
- axes : punch (-1 propre, +1 percutant), focus (-1 voix devant, +1 instru devant),
  width (-1 serré, +1 large). target : une cible (pente spectrale, crête, largeur),
  d'après les axes ou un morceau de référence.
- bounds : les bornes que tu ne peux pas franchir.

Ce que tu rends : un seul appel à mix.propose. Chaque réglage :
- kind volume (value en dB, valeur visée du fader), pan (value de -1 à 1),
  equaliser ou compressor (parameters : les valeurs visées des paramètres de l'effet
  du logiciel, dans leurs unités ; seulement ceux que tu changes).
  Égaliseur : hp_freq (coupe-bas, 20 = coupé), low_freq/low_gain/low_q,
  mid1_freq/mid1_gain/mid1_q, mid2_freq/mid2_gain/mid2_q, high_freq/high_gain/high_q.
  Compresseur : threshold_db, ratio, attack_ms, release_ms, makeup_db.
- sentence : une phrase en français simple, pour un débutant, qui dit la mesure qui
  justifie le réglage et ce que tu as fait. Exemple : « Sur ses coups, le kick ne
  passe la basse que de 1,3 dB à 63 Hz : j'ai creusé la basse de 3 dB à 63 Hz. »
- evidence : les mesures que la phrase cite, avec leur nom et leur valeur exacte telle
  que tu l'as reçue (arrondie au dixième) : "lufs", "crest", "truePeak", "correlation",
  "active", "peak10ms", "bands.<index>", "overlap.<index de l'octave>.<trackId de
  l'autre piste>" (valeur : share), "margin.<index de l'octave>.<trackId de l'autre
  piste>" (valeur : marginDb, entre un kick et une basse). Chaque nombre cité doit être écrit dans la phrase.

Règles :
- Les plugins de la personne ne se touchent pas, ne se retirent pas, ne se
  contournent pas. Les effets du logiciel se placent après eux.
- Une piste coupée (muted) ne se touche pas. Aucune piste ne se coupe.
- Reste dans les bornes. Un réglage hors bornes est refusé, et on te dira pourquoi.
- Ne change que ce qu'une mesure justifie. Moins de réglages justes valent mieux
  que beaucoup de réglages vagues.
- Pas de mastering, pas d'automation : des réglages fixes, piste par piste."""


def propose_tool() -> dict[str, Any]:
    """mix.propose: the proposal, in the shape the DAW reads (mix::Proposal)."""
    evidence = {
        "type": "object",
        "properties": {"measure": {"type": "string"}, "value": {"type": "number"}},
        "required": ["measure", "value"],
        "additionalProperties": False,
    }
    change = {
        "type": "object",
        "properties": {
            "trackId": {"type": "string"},
            "kind": {"type": "string", "enum": ["volume", "pan", "equaliser", "compressor"]},
            "value": {"type": "number"},
            "parameters": {"type": "object", "additionalProperties": {"type": "number"}},
            "sentence": {"type": "string"},
            "evidence": {"type": "array", "items": evidence},
        },
        "required": ["trackId", "kind", "sentence", "evidence"],
        "additionalProperties": False,
    }
    return {
        "name": PROPOSE_TOOL,
        "description": "Propose le mixage : les réglages, chacun avec sa phrase et ses mesures.",
        "input_schema": {
            "type": "object",
            "properties": {"changes": {"type": "array", "items": change}},
            "required": ["changes"],
            "additionalProperties": False,
        },
    }


class Mixer:
    """Decides a mix from a brief. Holds nothing between two calls."""

    def __init__(self, provider: IAProvider) -> None:
        self._provider = provider

    def decide(
        self,
        brief: dict[str, Any],
        previous: dict[str, Any] | None = None,
        refusals: list[dict[str, Any]] | None = None,
    ) -> dict[str, Any]:
        """{"proposal": {...}, "usage": {...}} or {"failed": True, "message": ...}.

        With `previous` and `refusals`, the model is shown what it proposed and
        what the DAW refused, and asked for the proposal again.
        """
        messages: list[dict[str, Any]] = [
            {"role": "user", "content": json.dumps(brief, ensure_ascii=False, separators=(",", ":"))}
        ]
        if previous is not None and refusals:
            said = "\n".join(f"- réglage {item.get('change')} : {item.get('why')}" for item in refusals)
            messages.append(
                {
                    "role": "assistant",
                    "content": json.dumps(previous, ensure_ascii=False, separators=(",", ":")),
                }
            )
            messages.append(
                {
                    "role": "user",
                    "content": "Le logiciel a refusé ces réglages :\n"
                    + said
                    + "\nRends la proposition entière, corrigée, en un seul appel à mix.propose.",
                }
            )

        try:
            turn = self._provider.converse(
                MIXING_PROMPT, messages, [propose_tool()], max_tokens=MIX_MAX_TOKENS, effort=MIX_EFFORT
            )
        except ProviderUnavailable as failure:
            return {"failed": True, "message": str(failure)}

        for call in turn.tool_calls:
            if call.name == PROPOSE_TOOL and turn.stop_reason != "max_tokens":
                changes = call.arguments.get("changes")
                if not isinstance(changes, list):
                    break
                return {
                    "proposal": {"decidedBy": self._provider.model, "changes": changes},
                    "usage": turn.usage.as_dict(),
                }

        return {
            "failed": True,
            "message": turn.text.strip() or "Le modèle n'a pas proposé de mixage.",
            "usage": turn.usage.as_dict(),
        }


__all__ = ["MIXING_PROMPT", "MIX_EFFORT", "MIX_MAX_TOKENS", "PROPOSE_TOOL", "Mixer", "propose_tool"]
