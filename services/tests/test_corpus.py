"""The corpus pipeline, on corpora written here: nothing of the user's work is in the repository."""

from __future__ import annotations

import json
import os
from pathlib import Path

import pytest

from daw_services.__main__ import main
from daw_services.harmony import corpus, midi
from daw_services.harmony.theory import Key, detect_chord, detect_key, diatonic_index, parse_key

FIXTURE = Path(__file__).parent / "fixtures" / "style-small.json"
PPQ = 96
STEP = PPQ // 4

# Two bars of a minor line on the sixteenth grid: (step, length, degree offset from the tonic).
PHRASE = [
    (0, 2, 0),
    (2, 2, 3),
    (4, 4, 7),
    (8, 2, 5),
    (10, 2, 3),
    (12, 4, 2),
    (16, 3, 0),
    (19, 1, -2),
    (20, 4, 0),
]


def line(tonic: int, octave: int = 5, channel: int = 0, name: str = "Lead") -> midi.MidiTrack:
    notes = [
        midi.MidiNote(
            12 * octave + tonic + offset,
            100 if step % 4 == 0 else 80,
            step * STEP,
            (step + length) * STEP,
            channel,
        )
        for step, length, offset in PHRASE
    ]
    return midi.MidiTrack(name=name, notes=notes)


def chords(tonic: int) -> midi.MidiTrack:
    # i, VI, VII, i in natural minor: one bar each.
    shapes = [(0, 3, 7), (8, 12, 15), (10, 14, 17), (0, 3, 7)]
    notes = [
        midi.MidiNote(48 + tonic + interval, 90, bar * 16 * STEP, (bar + 1) * 16 * STEP, 0)
        for bar, shape in enumerate(shapes)
        for interval in shape
    ]
    return midi.MidiTrack(name="Keys", notes=notes)


def drums() -> midi.MidiTrack:
    notes = [
        midi.MidiNote(42, 90, step * STEP, (step + 1) * STEP, midi.DRUM_CHANNEL) for step in range(0, 32, 2)
    ]
    return midi.MidiTrack(name="Hats", notes=notes)


def save(folder: Path, name: str, *tracks: midi.MidiTrack, bpm: float = 140.0) -> Path:
    path = folder / name
    path.write_bytes(midi.write(midi.MidiFile(PPQ, list(tracks), tempo_bpm=bpm)))
    return path


@pytest.fixture
def raw(tmp_path: Path) -> Path:
    folder = tmp_path / "raw"
    folder.mkdir()
    save(folder, "a-minor.mid", line(9), chords(9), drums())
    save(folder, "f-sharp-minor.mid", line(6, octave=5), chords(6), bpm=150.0)
    return folder


# --- the file format -------------------------------------------------------------


def test_a_file_written_reads_back() -> None:
    song = midi.MidiFile(PPQ, [line(9), drums()], tempo_bpm=140.0)
    back = midi.read(midi.write(song))
    assert back.ticks_per_beat == PPQ
    assert back.tempo_bpm == pytest.approx(140.0, abs=0.01)
    assert back.tracks[0].name == "Lead"
    assert back.tracks[0].notes == song.tracks[0].notes
    assert back.tracks[1].notes[0].channel == midi.DRUM_CHANNEL


def test_running_status_and_note_on_at_zero_velocity() -> None:
    # One track: note on 60, then running status for a note on 60 at velocity 0 (an off).
    body = bytes([0x00, 0x90, 60, 100, 0x60, 60, 0, 0x00, 0xFF, 0x2F, 0x00])
    data = (
        b"MThd"
        + (6).to_bytes(4, "big")
        + bytes([0, 0, 0, 1, 0, PPQ])
        + b"MTrk"
        + len(body).to_bytes(4, "big")
        + body
    )
    song = midi.read(data)
    assert song.tracks[0].notes == [midi.MidiNote(60, 100, 0, 0x60, 0)]


def test_a_file_that_is_not_midi_is_refused() -> None:
    with pytest.raises(midi.MidiError):
        midi.read(b"RIFF....")


# --- the rules, the same as the C++ ones -------------------------------------------


def test_the_rules_agree_with_the_generator() -> None:
    # The cases GenerationTests.cpp pins for Harmony.cpp.
    a_minor = Key(9, True)
    for pitch in range(128):
        index = diatonic_index(pitch, a_minor)
        assert (index is not None) == ((pitch - 9) % 12 in (0, 2, 3, 5, 7, 8, 10))
    assert diatonic_index(69, a_minor) == 35
    assert detect_key([(p, 1.0) for p in (57, 60, 64, 57, 59, 62, 64, 65, 67)]) == a_minor
    assert detect_chord([(53, 1.0), (57, 1.0), (60, 1.0)], a_minor) == 5
    assert detect_chord([(57, 1.0), (60, 1.0), (64, 1.0)], a_minor) == 0
    assert parse_key("F#m") == Key(6, True)
    assert parse_key("Bb") == Key(10, False)
    assert parse_key("sombre") is None


# --- the pipeline -------------------------------------------------------------------


def test_the_same_phrase_in_two_keys_counts_as_one_thing(raw: Path) -> None:
    model, report = corpus.build(raw)
    assert report.kept == 2
    assert report.keys == {"Am": 1, "F#m": 1}
    assert report.out_of_key == 0

    melody = model["roles"]["melody"]
    # Every interval context was seen exactly twice: once per key.
    assert all(count % 2 == 0 for counts in melody["interval"].values() for count in counts.values())
    assert melody["interval"][""]["2"] == 4  # a third up, A to C then C to E: twice per phrase, two keys


def test_roles_are_read_from_the_tracks(raw: Path) -> None:
    model, report = corpus.build(raw)
    assert report.tracks == {"melody": 2, "bass": 0, "chords": 2, "rhythm": 1}
    # The chords give i VI VII i, in degrees, in both keys.
    progression = model["roles"]["chords"]["progression"]
    assert progression["0"] == {"5": 2}
    assert progression["0,5"] == {"6": 2}
    assert progression["0,5,6"] == {"0": 2}
    # The hats: one onset every eighth.
    assert model["roles"]["rhythm"]["rhythm"]["|"] == {"2": 15}


def test_duplicates_and_played_tracks_are_refused_and_said(raw: Path) -> None:
    (raw / "copie.mid").write_bytes((raw / "a-minor.mid").read_bytes())
    save(raw, "renamed.mid", line(9), chords(9), drums(), bpm=141.0)  # other bytes, same notes
    played = midi.MidiTrack(
        name="Played",
        notes=[midi.MidiNote(60 + i % 5, 90, i * STEP + 7, i * STEP + 20, 0) for i in range(16)],
    )
    save(raw, "joue.mid", played)
    (raw / "casse.mid").write_bytes(b"MThd\x00\x00\x00\x06\x00\x01\x00\x01")

    _, report = corpus.build(raw)
    assert report.kept == 2
    reasons = "\n".join(report.refused)
    assert "copie.mid: doublon" in reasons
    assert "renamed.mid: mêmes notes" in reasons
    assert "hors grille" in reasons
    assert "casse.mid: illisible" in reasons


def test_the_csv_overrides_what_the_notes_say(raw: Path, tmp_path: Path) -> None:
    annotations = tmp_path / "corpus.csv"
    annotations.write_text("file;bpm;key;roles\na-minor.mid;90;C;Lead=bass\n", encoding="utf-8")
    model, report = corpus.build(raw, annotations)
    assert report.key_sources == {"csv": 1, "detected": 1}
    assert report.keys == {"C": 1, "F#m": 1}
    assert report.tracks["bass"] == 1
    assert 90.0 in report.tempos
    assert model["roles"]["bass"]["interval"]


def test_the_contract_fixture_is_what_the_pipeline_writes(raw: Path) -> None:
    """The C++ side loads this file (GenerationTests.cpp): both ends are pinned to it."""
    model, _ = corpus.build(raw)
    model["stats"] = {}
    written = json.dumps(model, ensure_ascii=False, indent=1, sort_keys=True)
    if os.environ.get("DAW_UPDATE_FIXTURES"):
        FIXTURE.parent.mkdir(exist_ok=True)
        FIXTURE.write_text(written, encoding="utf-8")
    assert FIXTURE.read_text(encoding="utf-8") == written


def test_the_command_line_writes_the_model_and_the_report(raw: Path, tmp_path: Path) -> None:
    out = tmp_path / "generation" / "markov.json"
    assert main(["corpus", "build", "--raw", str(raw), "--out", str(out)]) == 0
    model = json.loads(out.read_text(encoding="utf-8"))
    assert model["format"] == "daw-ia.style"
    assert model["origin"] == "corpus"
    assert (
        (tmp_path / "generation" / "markov-rapport.txt")
        .read_text(encoding="utf-8")
        .startswith("fichiers : 2")
    )


def test_an_empty_corpus_says_so_and_writes_nothing(tmp_path: Path) -> None:
    out = tmp_path / "markov.json"
    assert main(["corpus", "build", "--raw", str(tmp_path / "absent"), "--out", str(out)]) == 1
    assert not out.exists()
