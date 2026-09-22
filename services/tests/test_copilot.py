"""The pipe, tested against a fake DAW and a scripted provider.

What these prove: that a request becomes one group, that a read is answered
before the commands are staged, that a $new: name becomes one identifier used
everywhere, and that every failure says something in French instead of
crashing.

What they cannot prove: that the screen moved. That is checked on the real
binary, with the real model, by looking at it. A test that questions the state
proves the state, never the effect.
"""

from __future__ import annotations

import json
import socket
import threading
from typing import Any

import pytest

from daw_services.copilot import Agent, Answer, resolve_new_ids
from daw_services.ia_provider import ProviderUnavailable, ScriptedProvider, ToolCall, Turn, Usage
from daw_services.rpc import Peer, RpcError

STATE: dict[str, Any] = {
    "tempo": {"originPointId": "01JBWQ7Z0000000000000TEMP0", "points": []},
    "tracks": [
        {
            "trackId": "01JBWQ7Z0000000000000TRACK",
            "index": 1,
            "name": "Basse",
            "volumeDb": -6.0,
            "clips": [{"clipId": "01JBWQ7Z0000000000000CL1P0", "noteCount": 2}],
            "plugins": [],
        }
    ],
    "transport": {"playing": False},
    "machinePlugins": {"total": 1, "listed": [{"format": "CLAP", "name": "Vital"}]},
}

NOTES: dict[str, Any] = {
    "clipId": "01JBWQ7Z0000000000000CL1P0",
    "notes": [
        {"id": "01JBWQ7Z0000000000000N0TE1", "pitch": 60},
        {"id": "01JBWQ7Z0000000000000N0TE2", "pitch": 64},
    ],
}


class FakeDaw:
    """A DAW that answers the four methods the copilot uses, and remembers."""

    def __init__(self, refuse: str | None = None) -> None:
        self.executed: list[dict[str, Any]] = []
        self.refuse = refuse
        self._listener = socket.socket()
        self._listener.bind(("127.0.0.1", 0))
        self._listener.listen(1)
        self.port = self._listener.getsockname()[1]
        self._peer: Peer | None = None
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        def serve() -> None:
            connection, _ = self._listener.accept()
            self._peer = Peer(
                connection,
                {
                    "state.get": lambda _params: STATE,
                    "tools.list": lambda _params: [
                        {"name": "track.set_volume", "description": "volume", "input_schema": {}},
                        {"name": "track.add", "description": "piste", "input_schema": {}},
                        {"name": "plugin.insert", "description": "plugin", "input_schema": {}},
                        {"name": "note.quantize", "description": "quantise", "input_schema": {}},
                    ],
                    "clip.notes": lambda _params: NOTES,
                    "plugins.find": lambda params: {"query": params.get("query", ""), "found": []},
                    "commands.execute": self._execute,
                },
            )
            self._peer.serve_forever()

        self._thread = threading.Thread(target=serve, daemon=True)
        self._thread.start()

    def _execute(self, params: dict[str, Any]) -> dict[str, Any]:
        if self.refuse is not None:
            raise RuntimeError(self.refuse)

        self.executed.append(params)
        return {"groupId": "01JBWQ7Z00000000000GR0UP0", "commandIds": ["01JBWQ7Z0000000000000CMD01"]}

    def stop(self) -> None:
        if self._peer is not None:
            self._peer.close()

        self._listener.close()


@pytest.fixture
def daw() -> Any:
    fake = FakeDaw()
    fake.start()
    yield fake
    fake.stop()


def connected(daw: FakeDaw) -> Peer:
    peer = Peer.connect(daw.port)
    threading.Thread(target=peer.serve_forever, daemon=True).start()
    return peer


def tool_turn(*calls: ToolCall, text: str = "") -> Turn:
    return Turn(
        text=text,
        tool_calls=list(calls),
        usage=Usage(input_tokens=1200, output_tokens=80),
        raw_content=[
            {"type": "tool_use", "id": call.call_id, "name": call.name, "input": call.arguments}
            for call in calls
        ],
        stop_reason="tool_use",
    )


def test_a_request_becomes_one_group(daw: FakeDaw) -> None:
    provider = ScriptedProvider(
        [
            tool_turn(
                ToolCall("a", "track.add", {"trackId": "$new:basse", "name": "Basse", "volumeDb": 0.0}),
                ToolCall("b", "plugin.insert", {"trackId": "$new:basse", "plugin": {"id": "$new:vital"}}),
            ),
            Turn(text="Piste Basse ajoutée avec Vital.", usage=Usage(input_tokens=900, output_tokens=30)),
        ]
    )

    answer = Agent(connected(daw), provider).answer("ajoute une piste Basse et mets-y Vital")

    assert not answer.failed
    assert answer.group_id
    assert len(daw.executed) == 1

    sent = daw.executed[0]
    assert sent["label"] == "ajoute une piste Basse et mets-y Vital"
    assert [command["type"] for command in sent["commands"]] == ["track.add", "plugin.insert"]

    # The same $new: name is the same identifier in both commands, or the
    # plugin would land on a track that does not exist.
    first = sent["commands"][0]["payload"]["trackId"]
    second = sent["commands"][1]["payload"]["trackId"]
    assert first == second
    assert len(first) == 26
    assert "$new" not in json.dumps(sent)

    # And the cost of both turns is counted, not just the last one.
    assert answer.usage.input_tokens == 2100
    assert answer.usage.output_tokens == 110


def test_a_read_is_answered_before_the_commands_are_staged(daw: FakeDaw) -> None:
    provider = ScriptedProvider(
        [
            tool_turn(ToolCall("a", "clip.get_notes", {"clipId": "01JBWQ7Z0000000000000CL1P0"})),
            tool_turn(
                ToolCall(
                    "b",
                    "note.quantize",
                    {
                        "clipId": "01JBWQ7Z0000000000000CL1P0",
                        "noteIds": ["01JBWQ7Z0000000000000N0TE1", "01JBWQ7Z0000000000000N0TE2"],
                        "gridBeats": 0.25,
                    },
                )
            ),
            Turn(text="Notes quantifiées en doubles-croches."),
        ]
    )

    answer = Agent(connected(daw), provider).answer("quantifie les notes du clip en doubles-croches")

    assert not answer.failed
    assert len(daw.executed) == 1
    assert daw.executed[0]["commands"][0]["payload"]["noteIds"] == [
        "01JBWQ7Z0000000000000N0TE1",
        "01JBWQ7Z0000000000000N0TE2",
    ]


def test_a_request_out_of_reach_changes_nothing(daw: FakeDaw) -> None:
    provider = ScriptedProvider([Turn(text="Je ne sais pas faire ça pour l'instant.")])

    answer = Agent(connected(daw), provider).answer("masterise le morceau")

    assert not answer.failed
    assert answer.text == "Je ne sais pas faire ça pour l'instant."
    assert daw.executed == []


def test_a_missing_key_is_said_in_french(daw: FakeDaw) -> None:
    class NoKey:
        model = "none"

        def converse(self, system: str, messages: Any, tools: Any) -> Turn:
            del system, messages, tools
            raise ProviderUnavailable("Aucune clé d'API.")

    answer = Agent(connected(daw), NoKey()).answer("monte le volume de la piste 2 de 3 dB")

    assert answer.failed
    assert "clé" in answer.text
    assert daw.executed == []


def test_a_refused_command_is_reported_and_leaves_nothing() -> None:
    refusing = FakeDaw(refuse="la piste n'existe pas")
    refusing.start()
    try:
        provider = ScriptedProvider(
            [
                tool_turn(ToolCall("a", "track.set_volume", {"trackId": "absent", "volumeDb": 0.0})),
                Turn(text="Volume monté."),
            ]
        )

        answer = Agent(connected(refusing), provider).answer("monte le volume de la piste 2 de 3 dB")

        assert answer.failed
        assert "refusé" in answer.text
        assert refusing.executed == []
    finally:
        refusing.stop()


def test_a_dead_daw_is_said_rather_than_raised() -> None:
    dead = FakeDaw()
    dead.start()
    peer = connected(dead)
    dead.stop()
    peer.close()

    answer = Agent(peer, ScriptedProvider([])).answer("monte le volume")

    assert answer.failed
    assert "DAW" in answer.text


def test_new_identifiers_are_minted_once_per_name() -> None:
    minted: dict[str, str] = {}
    payload = {"a": "$new:basse", "b": ["$new:basse", "$new:vital"], "c": "01JBWQ7Z0000000000000TRACK"}

    resolved = resolve_new_ids(payload, minted)

    assert resolved["a"] == resolved["b"][0]
    assert resolved["b"][1] != resolved["a"]
    assert resolved["c"] == "01JBWQ7Z0000000000000TRACK"
    assert all(len(value) == 26 for value in minted.values())


def test_an_answer_carries_its_cost() -> None:
    answer = Answer(text="fait", usage=Usage(input_tokens=10, output_tokens=2))
    assert answer.as_dict()["usage"] == {
        "inputTokens": 10,
        "outputTokens": 2,
        "cacheReadTokens": 0,
        "cacheWriteTokens": 0,
    }


def test_a_peer_reports_a_broken_link_instead_of_hanging() -> None:
    fake = FakeDaw()
    fake.start()
    peer = connected(fake)
    fake.stop()
    peer.close()

    with pytest.raises(RpcError):
        peer.request("state.get", timeout=2.0)


def test_a_command_name_survives_the_vendor_rule() -> None:
    """The API refuses a dot in a tool name; the DAW names every command with one."""
    from daw_services.ia_provider import _original_tool_name, _safe_tool_name

    assert _safe_tool_name("track.set_volume") == "track__set_volume"
    assert _original_tool_name("track__set_volume", {}) == "track.set_volume"

    # And the map wins over the guess, so a name that already held two
    # underscores comes back as it left.
    assert _original_tool_name("odd__name", {"odd__name": "odd__name"}) == "odd__name"
