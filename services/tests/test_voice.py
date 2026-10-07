"""The push-to-talk's transcription (S25), without weights: the words, the
project's names, the doubt, and the protocol replayed on the test set."""

from __future__ import annotations

import base64
import io
import json
import wave
from pathlib import Path

import numpy as np
import pytest

from daw_services.voice import RATE, Heard, Word, digest, samples_of, words_of
from daw_services.voice.doubt import WORD_SURE, judge, speech_seconds
from daw_services.voice.names import find
from daw_services.voice.service import ReplayTranscriber, run

SET = Path(__file__).parent / "voix"
TABLE = SET / "parakeet-entendu.json"
NAMES = [n for group in json.loads((SET / "noms.json").read_text(encoding="utf-8")).values() for n in group]


def speech(seconds: float = 2.0) -> np.ndarray:
    """A voiced-like signal: a tone that comes and goes, above a faint floor."""
    t = np.arange(int(seconds * RATE)) / RATE
    envelope = (np.sin(2 * np.pi * 3 * t) > -0.2).astype(np.float32)
    return (0.2 * np.sin(2 * np.pi * 180 * t) * envelope + 1e-4).astype(np.float32)


def sure(*words: str) -> Heard:
    return Heard(" ".join(words), [Word(w, 0.99, i * 0.3) for i, w in enumerate(words)])


def read(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as file:
        return samples_of(file.readframes(file.getnframes()))


def test_words_are_made_of_pieces_a_space_starts():
    tokens = [" M", "ets", " la", " ", "8", "0", "8", "."]
    logs = [-0.43, -0.02, 0.0, -0.01, 0.0, -0.01, 0.0, -2.0]
    words = words_of(tokens, logs, [0.0] * len(tokens))
    assert [w.text for w in words] == ["Mets", "la", "808."]
    # A word is as sure as its least sure piece, punctuation aside.
    assert words[0].confidence == pytest.approx(np.exp(-0.43), abs=1e-3)
    assert words[2].confidence > 0.98


def test_a_name_said_is_kept_a_name_misheard_is_proposed_a_rank_is_not_a_name():
    found = find("Mets Serum sur la piste Lead Pluck.".split(), NAMES)
    assert [(n.name, n.exact) for n in found] == [("Serum", True), ("Lead Pluck", True)]

    found = find("Mets Messerum sur la piste lead plaque.".split(), NAMES)
    assert [(n.heard, n.name, n.exact) for n in found][-1] == ("lead plaque", "Lead Pluck", False)

    assert find("Renomme la piste quatre en refrain.".split(), NAMES) == []
    assert find("Passant Fa dièse mineur à 140 BPM.".split(), NAMES) == []

    unknown = find("Mets un compresseur sur la piste Zorglub.".split(), NAMES)
    assert unknown and unknown[0].name is None


def test_a_clear_french_phrase_with_known_names_is_sure():
    verdict = judge(sure("Mets", "Serum", "sur", "la", "piste", "Lead", "Pluck."), speech(), NAMES)
    assert not verdict.doubtful, verdict.reasons


@pytest.mark.parametrize(
    ("heard", "samples", "reason"),
    [
        (Heard(""), np.zeros(RATE, np.float32), "rien entendu"),
        (sure("Euh."), speech(0.3), "trop peu de parole"),
        (sure("Euh."), speech(), "un seul mot"),
        (sure("Turn", "the", "bass", "up", "a", "little", "bit."), speech(), "pas du français"),
        (
            sure("Mets", "un", "compresseur", "sur", "la", "piste", "Zorglub."),
            speech(),
            "aucun nom du projet",
        ),
        (sure("Ajoute", "Valhalla", "Supermassif", "sur", "Pad", "Ambient."), speech(), "est-ce"),
    ],
)
def test_each_rule_makes_a_phrase_doubtful_and_says_why(heard, samples, reason):
    verdict = judge(heard, samples, NAMES)
    assert verdict.doubtful
    assert any(reason in r for r in verdict.reasons), verdict.reasons


def test_an_uncertain_word_is_marked():
    heard = Heard(
        "Ajoute du reverb",
        [Word("Ajoute", 0.99), Word("du", 0.99, 0.4), Word("reverb", WORD_SURE - 0.2, 0.6)],
    )
    verdict = judge(heard, speech(), NAMES)
    assert verdict.doubtful and verdict.uncertain == [2]
    assert verdict.reasons == ["des mots incertains"]


def test_silence_has_no_speech_and_a_voice_has_some():
    assert speech_seconds(np.zeros(RATE * 2, np.float32)) == 0.0
    assert speech_seconds(read(SET / "synthese" / "c01.wav")) > 1.0


def ask(requests: list[dict]) -> list[dict]:
    out = io.StringIO()
    run(None, TABLE, io.StringIO("\n".join(json.dumps(r) for r in requests) + "\n"), out)
    return [json.loads(line) for line in out.getvalue().splitlines()]


def pcm(samples: np.ndarray) -> str:
    return base64.b64encode(
        np.clip(np.round(samples * 32768), -32768, 32767).astype("<i2").tobytes()
    ).decode()


def test_the_protocol_replayed_every_built_doubt_is_doubtful():
    table = json.loads(TABLE.read_text(encoding="utf-8"))
    built = [i for i in table["runs"] if i.startswith("d")]
    assert len(built) >= 7
    answers = ask(
        [{"id": 0, "method": "load"}]
        + [
            {
                "id": n + 1,
                "method": "transcribe",
                "samples": pcm(read(SET / "synthese" / f"{i}.wav")),
                "names": NAMES,
            }
            for n, i in enumerate(built)
        ]
    )
    assert answers[0]["event"] == "loaded" and answers[0]["transcriber"] == "replay"
    for ident, heard in zip(built, answers[1:], strict=True):
        assert heard["event"] == "heard"
        assert heard["doubtful"], (ident, heard["text"])


def test_the_replay_hears_the_phrase_it_was_given_and_nothing_else():
    replay = ReplayTranscriber(TABLE)
    samples = read(SET / "synthese" / "c01.wav")
    assert digest(samples) in json.loads(TABLE.read_text(encoding="utf-8"))["heard"]
    assert "compresseur" in replay.transcribe(samples).text
    assert replay.transcribe(samples * 0.5).text == ""
