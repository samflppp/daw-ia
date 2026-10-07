"""NVIDIA Parakeet TDT 0.6B v3, int8, through sherpa-onnx (the `voix` extra).

Licences, read at the source on 7 October 2026:
  - the weights: CC-BY-4.0 (https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3);
    the DAW credits NVIDIA where it says what it runs;
  - sherpa-onnx, which runs them on the CPU and packs them in ONNX:
    Apache-2.0 (https://github.com/k2-fsa/sherpa-onnx).

The weights are downloaded on first use into the DAW's models folder, never
shipped, never in the repository: 487 MB to download, 641 MB on disk. Each
file is checked against its digest before it is used.

Measured on the founder's i5-8365U (machine busy): 2.2 s to load, 0.3 to
0.9 s for a phrase of 3 s, 850 MB of memory while loaded.
"""

from __future__ import annotations

import hashlib
import os
import tarfile
import tempfile
import time
import urllib.request
from collections.abc import Callable
from pathlib import Path

import numpy as np

from daw_services.voice import RATE, Heard, words_of

NAME = "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8"
URL = f"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/{NAME}.tar.bz2"
DOWNLOAD_BYTES = 487_170_055
FILES = {
    "encoder.int8.onnx": "acfc2b4456377e15d04f0243af540b7fe7c992f8d898d751cf134c3a55fd2247",
    "decoder.int8.onnx": "179e50c43d1a9de79c8a24149a2f9bac6eb5981823f2a2ed88d655b24248db4e",
    "joiner.int8.onnx": "3164c13fc2821009440d20fcb5fdc78bff28b4db2f8d0f0b329101719c0948b3",
    "tokens.txt": "d58544679ea4bc6ac563d1f545eb7d474bd6cfa467f0a6e2c1dc1c7d37e3c35d",
}

Progress = Callable[[float], None]


def default_folder() -> Path:
    base = os.environ.get("LOCALAPPDATA") or str(Path.home() / ".cache")
    return Path(base) / "DAW IA" / "models"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def installed(folder: Path) -> bool:
    """The four files there; their digests are checked once, at install."""
    return all((folder / NAME / name).is_file() for name in FILES)


def install(folder: Path, progress: Progress) -> None:
    """Downloads and unpacks the weights, checks every file, or raises."""
    target = folder / NAME
    folder.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=folder) as scratch:
        archive = Path(scratch) / f"{NAME}.tar.bz2"
        with urllib.request.urlopen(URL, timeout=60) as answer, archive.open("wb") as out:  # noqa: S310
            total = int(answer.headers.get("Content-Length") or DOWNLOAD_BYTES)
            done = 0
            while block := answer.read(1 << 20):
                out.write(block)
                done += len(block)
                progress(0.95 * done / total)
        with tarfile.open(archive, "r:bz2") as packed:
            for member in packed.getmembers():
                name = Path(member.name).name
                if member.isfile() and name in FILES:
                    member.name = name
                    packed.extract(member, Path(scratch) / "unpacked", filter="data")
        for name, expected in FILES.items():
            found = _sha256(Path(scratch) / "unpacked" / name)
            if found != expected:
                raise RuntimeError(f"{name} ne correspond pas à son empreinte ({found[:12]}…)")
        target.mkdir(parents=True, exist_ok=True)
        for name in FILES:
            os.replace(Path(scratch) / "unpacked" / name, target / name)
    progress(1.0)


class ParakeetTranscriber:
    name = "parakeet"

    def __init__(self, folder: Path | None = None, threads: int = 4) -> None:
        import sherpa_onnx  # the `voix` extra

        where = (folder or default_folder()) / NAME
        started = time.perf_counter()
        self._recognizer = sherpa_onnx.OfflineRecognizer.from_transducer(
            encoder=str(where / "encoder.int8.onnx"),
            decoder=str(where / "decoder.int8.onnx"),
            joiner=str(where / "joiner.int8.onnx"),
            tokens=str(where / "tokens.txt"),
            num_threads=threads,
            model_type="nemo_transducer",
        )
        self.load_seconds = time.perf_counter() - started

    def transcribe(self, samples: np.ndarray) -> Heard:
        stream = self._recognizer.create_stream()
        stream.accept_waveform(RATE, samples.astype(np.float32))
        self._recognizer.decode_stream(stream)
        result = stream.result
        tokens = list(result.tokens)
        return Heard(
            result.text.strip(),
            words_of(tokens, list(result.ys_log_probs or []), list(result.timestamps or [])),
        )
