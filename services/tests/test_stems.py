"""The separator's protocol and files, proven with the simulated separator.

The quality of a real model is proven locally (--verify-stems, and
tools/prove_separation.py), never here: the CI downloads no weights.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from daw_services.__main__ import main
from daw_services.stems import STEMS, FakeSeparator
from daw_services.stems.audio import Audio, read, write

RATE = 44100


def tone(frequency: float, seconds: float = 2.0, level: float = 0.2) -> np.ndarray:
    t = np.arange(int(seconds * RATE)) / RATE
    mono = level * np.sin(2 * np.pi * frequency * t)
    return np.stack([mono, mono]).astype(np.float32)


def sdr(estimate: np.ndarray, reference: np.ndarray) -> float:
    """Signal-to-distortion ratio, in dB: how close `estimate` is to `reference`."""
    error = np.sum((reference - estimate) ** 2)
    return float(10 * np.log10(np.sum(reference**2) / max(error, 1e-20)))


SOURCES = {"bass": tone(60.0), "vocals": tone(800.0), "drums": tone(6000.0), "other": tone(200.0)}


def test_each_stem_is_closer_to_its_source_than_to_the_mix() -> None:
    mix = sum(SOURCES.values())
    stems = FakeSeparator().separate(Audio(mix, RATE), lambda _: None)

    for name in STEMS:
        to_source = sdr(stems[name].samples, SOURCES[name])
        to_mix = sdr(stems[name].samples, mix)
        assert to_source > to_mix + 10.0, f"{name}: {to_source:.1f} dB to its source, {to_mix:.1f} to the mix"


def test_the_stems_add_up_to_the_mix() -> None:
    mix = sum(SOURCES.values())
    stems = FakeSeparator().separate(Audio(mix, RATE), lambda _: None)

    total = sum(stem.samples for stem in stems.values())
    assert sdr(total, mix) > 60.0


def test_a_24_bit_wav_reads_back_within_its_resolution(tmp_path: Path) -> None:
    samples = tone(440.0, seconds=0.5, level=0.9)
    write(tmp_path / "a.wav", Audio(samples, RATE))
    back = read(tmp_path / "a.wav")

    assert back.rate == RATE
    assert back.samples.shape == samples.shape
    assert np.max(np.abs(back.samples - samples)) < 2.0 / (1 << 23)


def test_the_command_line_writes_four_stems_and_says_where(tmp_path: Path, capsys) -> None:
    source = tmp_path / "mix.wav"
    write(source, Audio(sum(SOURCES.values()), RATE))

    code = main(["separate", "--model", "fake", "--input", str(source), "--output", str(tmp_path / "out")])
    events = [json.loads(line) for line in capsys.readouterr().out.splitlines()]

    assert code == 0
    progress = [event["value"] for event in events if event["event"] == "progress"]
    assert progress[0] == 0.0 and progress[-1] == 1.0
    assert progress == sorted(progress)
    done = events[-1]
    assert done["event"] == "done"
    assert done["model"] == "fake-bands-1"
    assert sorted(done["stems"]) == sorted(STEMS)
    for path in done["stems"].values():
        assert read(Path(path)).samples.shape == (2, int(2.0 * RATE))


def test_a_mono_file_gives_stereo_stems(tmp_path: Path, capsys) -> None:
    source = tmp_path / "mono.wav"
    write(source, Audio(tone(60.0)[:1], RATE))

    assert main(["separate", "--model", "fake", "--input", str(source), "--output", str(tmp_path / "o")]) == 0
    done = json.loads(capsys.readouterr().out.splitlines()[-1])
    assert read(Path(done["stems"]["bass"])).samples.shape[0] == 2


def test_a_missing_file_is_one_sentence_and_a_failure(tmp_path: Path, capsys) -> None:
    code = main(
        ["separate", "--model", "fake", "--input", str(tmp_path / "nope.wav"), "--output", str(tmp_path)]
    )
    last = json.loads(capsys.readouterr().out.splitlines()[-1])

    assert code == 1
    assert last["event"] == "error"
    assert last["message"].startswith("La séparation a échoué")


@pytest.mark.parametrize("name", ["fast", "best"])
def test_a_real_model_is_never_loaded_by_asking_for_it(name: str) -> None:
    # Naming a model must not import torch nor download anything: the CI has
    # neither, and the DAW asks for the signature before choosing to run.
    import sys

    from daw_services.stems import models

    assert "torch" not in sys.modules
    assert models.signature_of(name)


def test_the_signature_is_said_without_a_file(capsys) -> None:
    assert main(["separate", "--model", "fake", "--signature"]) == 0
    assert capsys.readouterr().out.strip() == "fake-bands-1"


def test_what_a_model_leaves_out_goes_to_the_rest() -> None:
    from daw_services.stems import completed

    mix = sum(SOURCES.values())
    # A model that misses half the voice and returns one frame short.
    stems = {name: Audio(source[:, :-1].copy(), RATE) for name, source in SOURCES.items()}
    stems["vocals"] = Audio(0.5 * SOURCES["vocals"][:, :-1], RATE)

    done = completed(Audio(mix, RATE), stems)
    total = sum(stem.samples for stem in done.values())
    assert total.shape == mix.shape
    assert sdr(total, mix) > 60.0
    # Only the rest takes the difference: the drums are the model's, padded.
    assert np.array_equal(done["drums"].samples[:, :-1], SOURCES["drums"][:, :-1])
    assert np.all(done["drums"].samples[:, -1] == 0.0)
