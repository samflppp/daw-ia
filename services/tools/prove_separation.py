"""Proves a separator on known sources mixed together (S22, step 11).

usage: python tools/prove_separation.py <fast|best|fake> <voice.wav> [seconds]

The four sources are built here — drums and bass synthesised, chords as
decaying harmonics, the voice a recording given on the command line (Windows
speech synthesis in S22) — mixed, separated, and each stem is compared with
its source and with the mix by SDR. Passes when every stem is closer to its
source than to the mix, and the stems add up to the mix.

Local only: a real model downloads its weights. The CI runs the simulated
separator through tests/test_stems.py.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np

from daw_services.stems import STEMS, completed, separator
from daw_services.stems.audio import Audio, read

RATE = 44100
BPM = 100.0


def sdr(estimate: np.ndarray, reference: np.ndarray) -> float:
    error = np.sum((reference - estimate) ** 2)
    return float(10 * np.log10(np.sum(reference**2) / max(error, 1e-20)))


def envelope(length: int, attack: float, decay: float) -> np.ndarray:
    t = np.arange(length) / RATE
    return np.minimum(t / attack, 1.0) * np.exp(-t / decay)


def drums(frames: int, rng: np.random.Generator) -> np.ndarray:
    out = np.zeros(frames)
    beat = int(RATE * 60.0 / BPM)
    for start in range(0, frames, beat):
        n = min(int(0.35 * RATE), frames - start)
        t = np.arange(n) / RATE
        if (start // beat) % 2 == 0:  # kick: a falling sine
            phase = 2 * np.pi * (50 * t + 60 * (1 - np.exp(-t / 0.03)) * 0.03)
            out[start : start + n] += 0.9 * np.sin(phase) * envelope(n, 0.001, 0.12)
        else:  # snare: noise and a tone
            out[start : start + n] += (
                0.5 * rng.standard_normal(n) + 0.3 * np.sin(2 * np.pi * 190 * t)
            ) * envelope(n, 0.001, 0.07)
    for start in range(0, frames, beat // 2):  # hats on the eighths
        n = min(int(0.05 * RATE), frames - start)
        noise = rng.standard_normal(n)
        hat = np.diff(noise, prepend=0.0)  # high-passed
        out[start : start + n] += 0.25 * hat * envelope(n, 0.0005, 0.015)
    return out


def bass(frames: int) -> np.ndarray:
    out = np.zeros(frames)
    bar = int(RATE * 4 * 60.0 / BPM)
    roots = [55.0, 43.65, 49.0, 41.2]  # A1, F1, G1, E1
    eighth = bar // 8
    for start in range(0, frames, eighth):
        n = min(eighth, frames - start)
        t = np.arange(n) / RATE
        f = roots[(start // bar) % 4]
        saw = sum(np.sin(2 * np.pi * f * k * t) / k for k in range(1, 8))
        out[start : start + n] += 0.35 * saw * envelope(n, 0.005, 0.25)
    return out


def chords(frames: int) -> np.ndarray:
    out = np.zeros(frames)
    bar = int(RATE * 4 * 60.0 / BPM)
    triads = [[220.0, 261.6, 329.6], [174.6, 220.0, 261.6], [196.0, 246.9, 293.7], [164.8, 207.7, 246.9]]
    for start in range(0, frames, bar // 2):
        n = min(bar // 2, frames - start)
        t = np.arange(n) / RATE
        tone = np.zeros(n)
        for f in triads[(start // bar) % 4]:
            tone += sum(np.sin(2 * np.pi * f * k * t) * 0.6**k for k in range(1, 6))
        out[start : start + n] += 0.12 * tone * envelope(n, 0.01, 0.8)
    return out


def voice(path: Path, frames: int) -> np.ndarray:
    recorded = read(path)
    mono = recorded.samples.mean(axis=0)
    if recorded.rate != RATE:
        positions = np.arange(int(mono.size * RATE / recorded.rate)) * recorded.rate / RATE
        mono = np.interp(positions, np.arange(mono.size), mono)
    return 0.8 * np.resize(mono, frames)


def stereo(mono: np.ndarray, pan: float = 0.0) -> np.ndarray:
    return np.stack([mono * (1 - pan) / 1.0, mono * (1 + pan) / 1.0]).astype(np.float32) / (1 + abs(pan))


def main() -> int:
    name, voice_path = sys.argv[1], Path(sys.argv[2])
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 20.0
    frames = int(seconds * RATE)
    rng = np.random.default_rng(22)

    sources = {
        "drums": stereo(drums(frames, rng)),
        "bass": stereo(bass(frames)),
        "other": stereo(chords(frames), pan=0.3),
        "vocals": stereo(voice(voice_path, frames)),
    }
    mix = sum(sources.values())
    peak = float(np.max(np.abs(mix)))
    if peak > 0.95:
        scale = 0.95 / peak
        sources = {key: value * scale for key, value in sources.items()}
        mix = mix * scale

    chosen = separator(name)
    started = time.perf_counter()
    mixed = Audio(mix.astype(np.float32), RATE)
    raw = chosen.separate(mixed, lambda _: None)
    took = time.perf_counter() - started
    total_raw = sum(stem.samples[:, :frames] for stem in raw.values())
    print(f"somme des sorties du modèle vers le mélange : {sdr(total_raw, mix):.1f} dB")
    stems = completed(mixed, raw)

    print(f"{chosen.signature()} : {seconds:.0f} s séparées en {took:.1f} s")
    print("stem      vers sa source   vers le mélange")
    passed = True
    for stem in STEMS:
        estimate = stems[stem].samples[:, :frames]
        to_source, to_mix = sdr(estimate, sources[stem]), sdr(estimate, mix)
        passed = passed and to_source > to_mix
        print(f"{stem:8}  {to_source:8.1f} dB       {to_mix:8.1f} dB")
    total = sum(stem.samples[:, :frames] for stem in stems.values())
    adds_up = sdr(total, mix)
    print(f"somme des stems vers le mélange : {adds_up:.1f} dB")
    passed = passed and adds_up > 20.0
    print("OK" if passed else "ÉCHEC")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
