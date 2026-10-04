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


LEVELS: dict[str, Any] = {
    "playing": True,
    "windowSeconds": 0.3,
    "strips": [
        {
            "strip": "01JBWQ7Z0000000000000TRACK",
            "name": "Basse",
            "peakDb": -9.5,
            "rmsDb": -18.2,
            "over": False,
        },
        {"strip": "master", "name": "Master", "peakDb": -3.0, "rmsDb": -12.4, "over": False},
    ],
}


class FakeDaw:
    """A DAW that answers the four methods the copilot uses, and remembers."""

    def __init__(self, refuse: str | None = None, check: Any = None) -> None:
        self.executed: list[dict[str, Any]] = []
        self.mixes: list[dict[str, Any]] = []
        self.separations: list[dict[str, Any]] = []
        self.checked: list[list[dict[str, Any]]] = []
        self.refuse = refuse
        # What commands.check answers; None plays a DAW from before it existed.
        self.check = check
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
                    "mix.levels": lambda _params: LEVELS,
                    "mix.start": self._mix_start,
                    "stems.separate": self._separate,
                    "commands.execute": self._execute,
                    **({"commands.check": self._check} if self.check is not None else {}),
                },
            )
            self._peer.serve_forever()

        self._thread = threading.Thread(target=serve, daemon=True)
        self._thread.start()

    def _separate(self, params: dict[str, Any]) -> dict[str, Any]:
        self.separations.append(params)
        return {"started": True, "status": "Séparation : 0 %"}

    def _mix_start(self, params: dict[str, Any]) -> dict[str, Any]:
        self.mixes.append(params)
        return {"started": True, "status": "Mesure du morceau…", "axes": params}

    def _execute(self, params: dict[str, Any]) -> dict[str, Any]:
        if self.refuse is not None:
            raise RuntimeError(self.refuse)

        self.executed.append(params)
        return {"groupId": "01JBWQ7Z00000000000GR0UP0", "commandIds": ["01JBWQ7Z0000000000000CMD01"]}

    def _check(self, params: dict[str, Any]) -> dict[str, Any]:
        self.checked.append(list(params["commands"]))
        return self.check(params["commands"])

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


def test_levels_are_read_from_the_daw_and_handed_to_the_model(daw: FakeDaw) -> None:
    provider = ScriptedProvider(
        [
            tool_turn(ToolCall("a", "mix.get_levels", {})),
            Turn(text="Le master culmine à -3 dBFS, sans saturation."),
        ]
    )

    answer = Agent(connected(daw), provider).answer("le master sature ?")

    assert not answer.failed
    assert daw.executed == []

    # What the model was handed back is what the DAW measured, not a summary.
    handed = provider.messages[-1][-1]["content"][0]["content"]
    assert '"peakDb":-3.0' in handed
    assert '"strip":"master"' in handed


def test_mixing_the_song_starts_the_mix_and_writes_nothing(daw: FakeDaw) -> None:
    provider = ScriptedProvider(
        [
            tool_turn(ToolCall("a", "mix.start", {"punch": 0.5})),
            Turn(text="Je mesure le morceau ; la proposition s'affiche dans la console."),
        ]
    )

    answer = Agent(connected(daw), provider).answer("mixe le morceau, plus percutant")

    assert not answer.failed
    assert daw.mixes == [{"punch": 0.5}]
    assert daw.executed == []
    handed = provider.messages[-1][-1]["content"][0]["content"]
    assert '"started":true' in handed


def test_separating_a_clip_asks_the_daw_and_writes_nothing_itself(daw: FakeDaw) -> None:
    provider = ScriptedProvider(
        [
            tool_turn(
                ToolCall("a", "stems.separate", {"clipId": "01JBWQ7Z0000000000000CLIP1", "quality": "fast"})
            ),
            Turn(text="Je sépare le clip ; la progression s'affiche dans la playlist."),
        ]
    )

    answer = Agent(connected(daw), provider).answer("sépare ce morceau en stems, vite")

    assert not answer.failed
    assert daw.separations == [{"clipId": "01JBWQ7Z0000000000000CLIP1", "quality": "fast"}]
    assert daw.executed == []
    assert "stems.separate" in [tool["name"] for tool in provider.calls[0]]


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


# --- a request that writes music -------------------------------------------------


def checked_daw(check: Any) -> FakeDaw:
    fake = FakeDaw(check=check)
    fake.start()
    return fake


def test_a_call_cut_by_the_length_limit_is_not_staged_and_is_asked_again() -> None:
    daw = checked_daw(lambda _commands: {"ok": True})
    try:
        cut = tool_turn(
            ToolCall("a", "track.add", {"trackId": "$new:keys", "name": "Keys", "volumeDb": 0.0}),
            ToolCall("b", "note.add", {}),
        )
        cut.stop_reason = "max_tokens"
        provider = ScriptedProvider([cut, Turn(text="Piste ajoutée.")])

        answer = Agent(connected(daw), provider).answer("ajoute une piste Keys")

        assert not answer.failed
        assert [command["type"] for command in daw.executed[0]["commands"]] == ["track.add"]
        told = provider.messages[-1][-1]["content"]
        assert told[1]["is_error"] is True
        assert "coupé" in told[1]["content"]
    finally:
        daw.stop()


def test_a_call_the_copy_refuses_is_taken_back_and_corrected_in_the_same_request() -> None:
    def check(commands: list[dict[str, Any]]) -> dict[str, Any]:
        for index, command in enumerate(commands):
            if command["payload"].get("volumeDb", 0.0) > 6.0:
                return {
                    "ok": False,
                    "index": index,
                    "message": "appel 1 (track.set_volume) : volume trop haut",
                }
        return {"ok": True}

    daw = checked_daw(check)
    try:
        provider = ScriptedProvider(
            [
                tool_turn(ToolCall("a", "track.set_volume", {"trackId": "T", "volumeDb": 40.0})),
                tool_turn(ToolCall("b", "track.set_volume", {"trackId": "T", "volumeDb": 6.0})),
                Turn(text="Volume à 6 dB."),
            ]
        )

        answer = Agent(connected(daw), provider).answer("monte la basse à fond")

        assert not answer.failed
        # The refused call went back to the model as its own result...
        first_results = provider.messages[1][-1]["content"]
        assert first_results[0]["is_error"] is True
        assert "volume trop haut" in first_results[0]["content"]
        # ...and only the corrected one was applied.
        assert [command["payload"]["volumeDb"] for command in daw.executed[0]["commands"]] == [6.0]
    finally:
        daw.stop()


def test_the_omnisphere_request_is_one_group_ending_in_one_generation() -> None:
    daw = checked_daw(lambda _commands: {"ok": True})
    try:
        omnisphere = {"format": "VST3", "identifier": "spectrasonics-omnisphere", "name": "Omnisphere"}
        provider = ScriptedProvider(
            [
                tool_turn(ToolCall("a", "plugins.search", {"query": "omnisphere"})),
                tool_turn(
                    ToolCall(
                        "b", "tempo.set_bpm", {"pointId": "01JBWQ7Z0000000000000TEMP0", "beatsPerMinute": 140}
                    ),
                    ToolCall(
                        "c", "track.add", {"trackId": "$new:omni", "name": "Omnisphere", "volumeDb": 0.0}
                    ),
                    ToolCall(
                        "d",
                        "plugin.insert",
                        {
                            "trackId": "$new:omni",
                            "plugin": {"id": "$new:synth", "ref": omnisphere, "bypassed": False},
                        },
                    ),
                    ToolCall(
                        "e",
                        "clip.create_midi",
                        {
                            "trackId": "$new:omni",
                            "clipId": "$new:accords",
                            "startBeats": 0.0,
                            "lengthBeats": 16.0,
                        },
                    ),
                    ToolCall(
                        "f",
                        "pattern.generate",
                        {"clipId": "$new:accords", "role": "chords", "key": {"tonic": "A", "mode": "minor"}},
                    ),
                ),
                Turn(text="Omnisphere chargé, tempo à 140, quatre mesures d'accords en la mineur."),
            ]
        )

        answer = Agent(connected(daw), provider).answer(
            "ouvre un omnisphere et cree des accords triste dans un pattern en 140 bpm"
        )

        assert not answer.failed
        sent = daw.executed[0]["commands"]
        assert [command["type"] for command in sent] == [
            "tempo.set_bpm",
            "track.add",
            "plugin.insert",
            "clip.create_midi",
            "pattern.generate",
        ]
        # The generation writes into the row created two calls before.
        assert sent[4]["payload"]["clipId"] == sent[3]["payload"]["clipId"]
        assert len(sent[4]["payload"]["clipId"]) == 26
        # Every turn with commands was tried on the copy first.
        assert daw.checked
    finally:
        daw.stop()


def test_the_model_is_told_that_a_progression_needs_several_bars() -> None:
    # The generator changes chord once per bar: asked for chords on a pattern of
    # one bar, it gives one chord. The rule that prevents it lives in the prompt,
    # since only the model decides the range it generates on.
    from daw_services.copilot import SYSTEM_PROMPT

    assert "qu'un accord" in SYSTEM_PROMPT
    assert "pattern.set_length" in SYSTEM_PROMPT
    assert "lengthBeats 16" in SYSTEM_PROMPT
