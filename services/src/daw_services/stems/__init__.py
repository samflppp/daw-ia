"""The stem separator (S22): one audio file in, four stems out.

Voice, drums, bass and the rest, each a WAV the size of the source, placed
where the source was. The DAW runs one separation per process
(`daw-services separate`), reads its progress line by line on stdout, and
cancels it by ending the process: nothing here has to stop cleanly, and a
crash takes nothing else down.

Three separators behind one interface:

  fake   band filters, deterministic, numpy only. The CI's separator: it
         proves the protocol, the files and the sum, never the quality.
  fast   HTDemucs (Meta), one model.
  best   the best published model (see models.py).

The models' weights are research-only today (see docs/bilan-s22.md): they
are downloaded on first use, never shipped, never in the repository.
"""

from __future__ import annotations

import json
import sys
import time
from collections.abc import Callable
from pathlib import Path
from typing import Protocol

import numpy as np

from daw_services.stems.audio import Audio, read, write

STEMS = ("vocals", "drums", "bass", "other")

Progress = Callable[[float], None]


class Separator(Protocol):
    """What every separator answers."""

    name: str

    def signature(self) -> str:
        """Names the model and its weights: a separation is cached under it."""
        ...

    def separate(self, audio: Audio, progress: Progress) -> dict[str, Audio]:
        """The four stems of `audio`, same rate, same length."""
        ...


class FakeSeparator:
    """Four bands of the spectrum, the rest being what they leave.

    bass under 150 Hz, vocals 300 Hz to 3.4 kHz, drums above 3.4 kHz, other
    the mix minus those three: the stems add up to the mix exactly. Nothing
    of a voice or a drum is recognised; that is the models' job.
    """

    name = "fake"

    def signature(self) -> str:
        return "fake-bands-1"

    def separate(self, audio: Audio, progress: Progress) -> dict[str, Audio]:
        progress(0.0)
        spectrum = np.fft.rfft(audio.samples, axis=1)
        frequencies = np.fft.rfftfreq(audio.samples.shape[1], 1.0 / audio.rate)

        def band(low: float, high: float) -> np.ndarray:
            kept = np.where((frequencies >= low) & (frequencies < high), spectrum, 0.0)
            return np.fft.irfft(kept, n=audio.samples.shape[1], axis=1).astype(np.float32)

        bass = band(0.0, 150.0)
        progress(0.33)
        vocals = band(300.0, 3400.0)
        progress(0.66)
        drums = band(3400.0, audio.rate / 2.0 + 1.0)
        other = (audio.samples - bass - vocals - drums).astype(np.float32)
        progress(1.0)
        return {
            "vocals": Audio(vocals, audio.rate),
            "drums": Audio(drums, audio.rate),
            "bass": Audio(bass, audio.rate),
            "other": Audio(other, audio.rate),
        }


def signature_of(name: str) -> str:
    """The signature of a separator, without loading it: the cache's key."""
    if name == "fake":
        return FakeSeparator().signature()
    from daw_services.stems import models as real

    return real.signature_of(name)


def separator(name: str, models: Path | None = None) -> Separator:
    """The separator a name asks for. The models load their weights lazily."""
    if name == "fake":
        return FakeSeparator()
    from daw_services.stems import models as real  # torch: the `stems` extra

    return real.separator(name, models)


def completed(mix: Audio, stems: dict[str, Audio]) -> dict[str, Audio]:
    """The stems, with what the model left out given to `other`.

    The DAW lays the stems in place of their source, which it removes: they
    must add up to it. A model's four outputs do not quite (HTDemucs misses
    some of the air above 12 kHz, about -24 dB in all on the signal of
    tools/prove_separation.py); the difference goes to the rest, the one
    stem that has no role of its own.
    """
    frames = mix.samples.shape[1]
    fitted = {}
    for name in STEMS:
        samples = stems[name].samples[:, :frames]
        if samples.shape[1] < frames:
            samples = np.pad(samples, ((0, 0), (0, frames - samples.shape[1])))
        fitted[name] = samples.astype(np.float32)
    left = mix.samples - sum(fitted.values())
    fitted["other"] = (fitted["other"] + left).astype(np.float32)
    return {name: Audio(samples, mix.rate) for name, samples in fitted.items()}


def _say(event: dict) -> None:
    print(json.dumps(event, ensure_ascii=False), flush=True)


def run(model: str, source: Path, out: Path, models: Path | None = None) -> int:
    """`daw-services separate`: the stems of `source` written in `out`.

    One JSON object per line on stdout:
      {"event": "progress", "value": 0.42}
      {"event": "done", "model": "<signature>", "stems": {"vocals": "<path>", ...}, "seconds": 12.3}
      {"event": "error", "message": "<sentence in French>"}
    """
    started = time.perf_counter()
    try:
        chosen = separator(model, models)
        audio = read(source)
        if audio.samples.shape[0] == 1:
            audio = Audio(np.repeat(audio.samples, 2, axis=0), audio.rate)

        last = [-1.0]

        def progress(value: float) -> None:
            value = min(max(value, 0.0), 1.0)
            if value >= 1.0 or value - last[0] >= 0.01:
                last[0] = value
                _say({"event": "progress", "value": round(value, 3)})

        stems = completed(audio, chosen.separate(audio, progress))
        written: dict[str, str] = {}
        for name in STEMS:
            path = out / f"{name}.wav"
            write(path, stems[name])
            written[name] = str(path)
        _say(
            {
                "event": "done",
                "model": chosen.signature(),
                "stems": written,
                "seconds": round(time.perf_counter() - started, 2),
            }
        )
        return 0
    except Exception as failure:  # one sentence for the DAW, the trace for daw.log
        _say({"event": "error", "message": f"La séparation a échoué : {failure}"})
        print(repr(failure), file=sys.stderr, flush=True)
        return 1
