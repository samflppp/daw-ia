"""Reading and writing the audio the separator works on.

A WAV in PCM is read and written here with the standard library and numpy, so
the simulated separator, its tests and the CI need nothing else. Any other
format (MP3, FLAC, OGG, AIFF, float WAV) is read through soundfile, which
comes with the models (the `stems` extra) and is never needed in the CI.
"""

from __future__ import annotations

import wave
from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass(frozen=True)
class Audio:
    """Samples as float32, shape (channels, frames), and their rate."""

    samples: np.ndarray
    rate: int

    @property
    def seconds(self) -> float:
        return self.samples.shape[1] / self.rate if self.rate else 0.0


def _read_pcm(path: Path) -> Audio:
    with wave.open(str(path), "rb") as file:
        channels = file.getnchannels()
        width = file.getsampwidth()
        rate = file.getframerate()
        raw = file.readframes(file.getnframes())

    if width == 2:
        values = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 3:
        bytes3 = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        ints = bytes3[:, 0] | (bytes3[:, 1] << 8) | (bytes3[:, 2] << 16)
        ints = np.where(ints >= 1 << 23, ints - (1 << 24), ints)
        values = ints.astype(np.float32) / float(1 << 23)
    elif width == 4:
        values = np.frombuffer(raw, dtype="<i4").astype(np.float32) / float(1 << 31)
    else:
        raise ValueError(f"{path.name} : {8 * width} bits par échantillon, non lu")

    return Audio(values.reshape(-1, channels).T.copy(), rate)


def read(path: Path) -> Audio:
    """Any file the DAW takes as a sample, as float32 (channels, frames)."""
    try:
        return _read_pcm(path)
    except (wave.Error, ValueError, EOFError):
        pass

    import soundfile  # the `stems` extra: only a real separation needs it

    data, rate = soundfile.read(str(path), dtype="float32", always_2d=True)
    return Audio(np.ascontiguousarray(data.T), int(rate))


def write(path: Path, audio: Audio) -> None:
    """A 24-bit PCM WAV: what the DAW's content store keeps for a stem."""
    clipped = np.clip(audio.samples, -1.0, 1.0 - 1.0 / (1 << 23))
    ints = np.round(clipped.T.reshape(-1) * float(1 << 23)).astype(np.int32)
    packed = np.empty((ints.size, 3), dtype=np.uint8)
    packed[:, 0] = ints & 0xFF
    packed[:, 1] = (ints >> 8) & 0xFF
    packed[:, 2] = (ints >> 16) & 0xFF

    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as file:
        file.setnchannels(audio.samples.shape[0])
        file.setsampwidth(3)
        file.setframerate(audio.rate)
        file.writeframes(packed.tobytes())
