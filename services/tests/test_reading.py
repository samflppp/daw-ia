"""The generation window's prompt, read by the model (S16), against a scripted provider.

What these prove: the reading tool is pattern.generate's schema without the
placement, what the model answers comes back in the S14 Interpretation shape,
the call is short and light, and every failure is a French sentence for the
window to fall back on. That the words are understood is proved on the real
binary, with the real model.
"""

from __future__ import annotations

import socket
import threading
from typing import Any

import pytest

from daw_services.copilot.reading import (
    READ_EFFORT,
    READ_MAX_TOKENS,
    READ_TOOL,
    Reader,
    interpretation_of,
    reading_tool,
)
from daw_services.ia_provider import ProviderUnavailable, ScriptedProvider, ToolCall, Turn, Usage
from daw_services.rpc import Peer

GENERATE = {
    "name": "pattern.generate",
    "description": "Écrit une ligne musicale.",
    "input_schema": {
        "type": "object",
        "properties": {
            "clipId": {"type": "string"},
            "fromBeats": {"type": "number"},
            "toBeats": {"type": "number"},
            "key": {"type": "object"},
            "role": {"type": "string", "enum": ["melody", "bass", "chords", "rhythm"]},
            "density": {"type": "string", "enum": ["sparse", "medium", "dense"]},
            "form": {"type": "string"},
            "variant": {"type": "integer"},
        },
        "required": ["clipId"],
    },
}


class FakeDaw:
    def __init__(self, tools: list[dict[str, Any]]) -> None:
        self.tools = tools
        self.asked = 0
        self._listener = socket.socket()
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self.port = self._listener.getsockname()[1]
        self._peer: Peer | None = None

    def start(self) -> None:
        def serve() -> None:
            connection, _ = self._listener.accept()
            self._peer = Peer(connection, {"tools.list": self._list})
            self._peer.serve_forever()

        threading.Thread(target=serve, daemon=True).start()

    def _list(self, _params: dict[str, Any]) -> list[dict[str, Any]]:
        self.asked += 1
        return self.tools

    def stop(self) -> None:
        if self._peer is not None:
            self._peer.close()
        self._listener.close()


@pytest.fixture
def daw() -> Any:
    fake = FakeDaw([{"name": "track.add", "input_schema": {}}, GENERATE])
    fake.start()
    yield fake
    fake.stop()


def connected(daw: FakeDaw) -> Peer:
    peer = Peer.connect(daw.port)
    threading.Thread(target=peer.serve_forever, daemon=True).start()
    return peer


def read_turn(arguments: dict[str, Any], stop: str = "tool_use") -> Turn:
    call = ToolCall(call_id="toolu_1", name=READ_TOOL, arguments=arguments)
    return Turn(
        tool_calls=[call],
        usage=Usage(input_tokens=900, output_tokens=40),
        raw_content=[{"type": "tool_use", "id": "toolu_1", "name": READ_TOOL, "input": arguments}],
        stop_reason=stop,
    )


ZONE = {"lengthBeats": 16, "beatsPerBar": 4, "hasNotes": False, "tracks": ["Keys"]}


def test_the_reading_tool_is_pattern_generate_without_the_placement() -> None:
    tool = reading_tool(GENERATE)
    properties = tool["input_schema"]["properties"]
    assert tool["name"] == READ_TOOL
    assert set(properties) == {"key", "role", "density", "form", "transform", "ignored"}
    assert "keep_rhythm" in properties["transform"]["enum"]
    assert properties["role"] == GENERATE["input_schema"]["properties"]["role"]
    assert "required" not in tool["input_schema"]


def test_sad_chords_come_back_as_an_interpretation(daw: FakeDaw) -> None:
    answer = {"key": {"tonic": "A", "mode": "minor"}, "role": "chords", "density": "sparse", "ignored": []}
    provider = ScriptedProvider([read_turn(answer)])
    reader = Reader(connected(daw), provider)

    read = reader.read("des accords tristes et calmes", ZONE)

    assert read["interpretation"] == {
        "key": {"tonic": "A", "mode": "minor"},
        "role": "chords",
        "density": "sparse",
        "ignored": [],
        "conflicts": [],
    }
    assert read["usage"]["inputTokens"] == 900
    # One tool, the reading one; the zone and the words are in the message.
    assert [tool["name"] for tool in provider.calls[0]] == [READ_TOOL]
    assert "des accords tristes et calmes" in provider.messages[0][0]["content"]
    assert '"tracks":["Keys"]' in provider.messages[0][0]["content"]
    assert provider.options[0] == {"max_tokens": READ_MAX_TOKENS, "effort": READ_EFFORT}


def test_the_schema_is_asked_once(daw: FakeDaw) -> None:
    provider = ScriptedProvider([read_turn({"role": "bass"}), read_turn({"role": "melody"})])
    reader = Reader(connected(daw), provider)
    reader.read("une basse", ZONE)
    reader.read("une mélodie", ZONE)
    assert daw.asked == 1


def test_words_left_over_are_said() -> None:
    assert interpretation_of({"role": "melody", "ignored": ["planant", " "], "clipId": "x"}) == {
        "role": "melody",
        "ignored": ["planant"],
        "conflicts": [],
    }


def test_a_cut_call_is_a_failure_not_a_reading(daw: FakeDaw) -> None:
    provider = ScriptedProvider([read_turn({"role": "cho"}, stop="max_tokens")])
    read = Reader(connected(daw), provider).read("des accords", ZONE)
    assert read["failed"] is True


def test_a_model_that_only_talks_fails_in_french(daw: FakeDaw) -> None:
    provider = ScriptedProvider([Turn(text="Je ne comprends pas.")])
    read = Reader(connected(daw), provider).read("???", ZONE)
    assert read == {"failed": True, "message": "Je ne comprends pas.", "usage": Usage().as_dict()}


def test_no_key_is_a_failure_the_window_can_fall_back_on(daw: FakeDaw) -> None:
    class Unavailable(ScriptedProvider):
        def converse(self, *args: Any, **kwargs: Any) -> Turn:
            raise ProviderUnavailable("Aucune clé d'API.")

    read = Reader(connected(daw), Unavailable([])).read("des accords", ZONE)
    assert read == {"failed": True, "message": "Aucune clé d'API."}


def test_a_daw_without_the_generator_is_said() -> None:
    fake = FakeDaw([{"name": "track.add", "input_schema": {}}])
    fake.start()
    try:
        read = Reader(connected(fake), ScriptedProvider([])).read("des accords", ZONE)
        assert read["failed"] is True
        assert "pattern.generate" in read["message"]
    finally:
        fake.stop()


def test_notes_in_the_zone_are_reworked_as_the_model_says(daw: FakeDaw) -> None:
    answer = {"transform": "darker", "ignored": []}
    provider = ScriptedProvider([read_turn(answer), read_turn(answer)])
    reader = Reader(connected(daw), provider)

    read = reader.read("rends-le plus sombre", {**ZONE, "hasNotes": True})
    assert read["transform"] == "darker"
    assert "transform" not in read["interpretation"]

    # An empty zone has nothing to rework: the transformation is not passed on.
    read = reader.read("rends-le plus sombre", ZONE)
    assert "transform" not in read
