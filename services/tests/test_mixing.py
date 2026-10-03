"""The mix decided by the model (S20), against a scripted provider.

What these prove: the model is sent the brief as numbers and nothing else, it
answers with one mix.propose call that comes back in the shape the DAW reads,
a refusal of the DAW is shown to it with its reason, and every failure is a
French sentence. That the decision is good is judged by ear, on the real
binary; that it stays in bounds is the DAW's guards, tested in C++.
"""

from __future__ import annotations

import json

from daw_services.copilot.mixing import MIX_EFFORT, MIX_MAX_TOKENS, PROPOSE_TOOL, Mixer, propose_tool
from daw_services.ia_provider import ProviderUnavailable, ScriptedProvider, ToolCall, Turn, Usage

BRIEF = {
    "bandsHz": [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000],
    "tracks": [
        {"trackId": "01KICK00000000000000000000", "role": "kick", "measure": {"lufs": -14.0}},
        {"trackId": "01BASS00000000000000000000", "role": "bass", "measure": {"lufs": -12.0}},
    ],
    "masking": [
        {"a": "01KICK00000000000000000000", "b": "01BASS00000000000000000000", "band": 1, "share": 72}
    ],
}

CARVE = {
    "trackId": "01BASS00000000000000000000",
    "kind": "equaliser",
    "parameters": {"mid1_freq": 63, "mid1_gain": -3, "mid1_q": 1.4},
    "sentence": "La basse et le kick se recouvrent à 63 Hz 72 % du temps : j'ai creusé la basse de 3 dB.",
    "evidence": [{"measure": "overlap.1.01KICK00000000000000000000", "value": 72}],
}


def proposing(changes: list[dict], text: str = "") -> Turn:
    return Turn(
        text=text,
        tool_calls=[ToolCall(call_id="toolu_1", name=PROPOSE_TOOL, arguments={"changes": changes})],
        usage=Usage(input_tokens=5200, output_tokens=900),
        stop_reason="tool_use",
    )


def test_the_model_reads_numbers_and_answers_one_proposal() -> None:
    provider = ScriptedProvider([proposing([CARVE])], model="claude-test")
    answer = Mixer(provider).decide(BRIEF)

    assert answer["proposal"] == {"decidedBy": "claude-test", "changes": [CARVE]}
    assert answer["usage"]["inputTokens"] == 5200
    # One tool, one message holding the brief as JSON, and nothing else.
    assert [tool["name"] for tool in provider.calls[0]] == [PROPOSE_TOOL]
    assert len(provider.messages[0]) == 1
    assert json.loads(provider.messages[0][0]["content"]) == BRIEF
    assert provider.options[0] == {"max_tokens": MIX_MAX_TOKENS, "effort": MIX_EFFORT}


def test_a_refusal_is_shown_to_the_model_with_its_reason() -> None:
    out_of_bounds = {**CARVE, "parameters": {"mid1_gain": -12}}
    provider = ScriptedProvider([proposing([CARVE])])
    previous = {"changes": [out_of_bounds]}
    refusals = [{"trackId": "01BASS00000000000000000000", "change": 0, "why": "au-delà de -6 à +4 dB"}]

    answer = Mixer(provider).decide(BRIEF, previous, refusals)

    assert answer["proposal"]["changes"] == [CARVE]
    shown = provider.messages[0]
    assert [message["role"] for message in shown] == ["user", "assistant", "user"]
    assert json.loads(shown[1]["content"]) == previous
    assert "au-delà de -6 à +4 dB" in shown[2]["content"]


def test_no_proposal_is_a_french_sentence() -> None:
    silent = Mixer(ScriptedProvider([Turn(text="Je ne sais pas.")])).decide(BRIEF)
    assert silent == {"failed": True, "message": "Je ne sais pas.", "usage": silent["usage"]}

    class Absent:
        model = "absent"

        def converse(self, *args, **kwargs):  # noqa: ANN002, ANN003, ANN202
            raise ProviderUnavailable("Aucune clé d'API.")

    assert Mixer(Absent()).decide(BRIEF) == {"failed": True, "message": "Aucune clé d'API."}


def test_a_truncated_answer_is_not_a_proposal() -> None:
    cut = proposing([CARVE])
    cut.stop_reason = "max_tokens"
    assert Mixer(ScriptedProvider([cut])).decide(BRIEF)["failed"] is True


def test_the_tool_has_the_shape_of_the_daws_proposal() -> None:
    schema = propose_tool()["input_schema"]
    item = schema["properties"]["changes"]["items"]
    assert item["properties"]["kind"]["enum"] == ["volume", "pan", "equaliser", "compressor"]
    assert set(item["required"]) == {"trackId", "kind", "sentence", "evidence"}
