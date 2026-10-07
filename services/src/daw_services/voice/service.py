"""`daw-services voix`: the transcriber, kept loaded between two phrases.

The DAW starts it at the first press of the key and ends it after a quarter
of an hour without a phrase: the model holds 850 MB while it lives. One JSON
object per line, both ways:

  in   {"id": 1, "method": "load"}
  out  {"id": 1, "event": "loaded", "transcriber": "parakeet", "seconds": 2.2}

  in   {"id": 2, "method": "transcribe", "samples": "<base64 PCM, 16 kHz, mono, 16 bits>",
        "names": ["Lead Pluck", "Serum", ...]}
  out  {"id": 2, "event": "heard", "text": "...", "words": [{"text", "confidence", "uncertain"}],
        "doubtful": true, "reasons": ["..."], "names": [{"heard", "name", "exact"}],
        "speech": 2.4, "seconds": 0.41}

  out  {"id": n, "event": "error", "code": "absent" | "failed", "message": "<French>"}

`daw-services voix --install` downloads the weights, its progress on stdout:
  {"event": "progress", "value": 0.42} … {"event": "installed"}

Nothing is written: the samples live for one request.
"""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path
from typing import IO

from daw_services.voice import Heard, Transcriber, Word, decode, digest, homophones
from daw_services.voice.doubt import judge, spoken_seconds


class ReplayTranscriber:
    """What Parakeet heard on the test set, replayed by the audio's digest.

    The CI's transcriber: no weights, no model. A phrase it was never given
    is heard as nothing, which is doubtful, which acts on nothing.
    """

    name = "replay"

    def __init__(self, table: Path) -> None:
        raw = json.loads(table.read_text(encoding="utf-8"))
        self._heard = {
            key: Heard(entry["text"], [Word(w[0], w[1], w[2]) for w in entry["words"]])
            for key, entry in raw["heard"].items()
        }

    def transcribe(self, samples) -> Heard:
        return self._heard.get(digest(samples), Heard(""))


def _say(out: IO[str], event: dict) -> None:
    out.write(json.dumps(event, ensure_ascii=False) + "\n")
    out.flush()


def answer(transcriber: Transcriber, request: dict) -> dict:
    """The answer to one `transcribe` request."""
    samples = decode(request.get("samples", ""))
    started = time.perf_counter()
    heard = homophones(transcriber.transcribe(samples))
    elapsed = time.perf_counter() - started
    verdict = judge(heard, samples, list(request.get("names", [])))
    return {
        "event": "heard",
        "text": heard.text,
        "words": [
            {
                "text": w.text,
                "confidence": w.confidence,
                "uncertain": i in verdict.uncertain,
                "heard": w.heard,
            }
            for i, w in enumerate(heard.words)
        ],
        "doubtful": verdict.doubtful,
        "reasons": verdict.reasons,
        "names": [{"heard": n.heard, "name": n.name, "exact": n.exact} for n in verdict.names],
        "speech": round(spoken_seconds(heard, samples), 2),
        "seconds": round(elapsed, 3),
    }


def _failed(out: IO[str], ident, code: str, message: str) -> None:
    _say(out, {"id": ident, "event": "error", "code": code, "message": message})


def run(
    models: Path | None, replay: Path | None, stdin: IO[str] = sys.stdin, stdout: IO[str] = sys.stdout
) -> int:
    transcriber: Transcriber | None = None
    for line in stdin:
        if not line.strip():
            continue
        request = json.loads(line)
        ident = request.get("id")
        try:
            loaded = 0.0
            if transcriber is None:
                started = time.perf_counter()
                if replay is not None:
                    transcriber = ReplayTranscriber(replay)
                else:
                    from daw_services.voice import parakeet

                    if not parakeet.installed(models or parakeet.default_folder()):
                        _failed(stdout, ident, "absent", "La reconnaissance vocale n'est pas installée.")
                        continue
                    transcriber = parakeet.ParakeetTranscriber(models)
                loaded = round(time.perf_counter() - started, 2)
            if request.get("method") == "load":
                _say(
                    stdout,
                    {"id": ident, "event": "loaded", "transcriber": transcriber.name, "seconds": loaded},
                )
            elif request.get("method") == "transcribe":
                _say(stdout, {"id": ident, **answer(transcriber, request), "loaded": loaded})
            else:
                _failed(stdout, ident, "failed", "requête inconnue")
        except Exception as failure:  # one sentence for the DAW, the trace for daw.log
            _failed(stdout, ident, "failed", f"La transcription a échoué : {failure}")
            print(repr(failure), file=sys.stderr, flush=True)
    return 0


def install(models: Path | None, stdout: IO[str] = sys.stdout) -> int:
    from daw_services.voice import parakeet

    folder = models or parakeet.default_folder()
    last = [-1.0]

    def progress(value: float) -> None:
        if value >= 1.0 or value - last[0] >= 0.01:
            last[0] = value
            _say(stdout, {"event": "progress", "value": round(value, 3)})

    try:
        parakeet.install(folder, progress)
        _say(stdout, {"event": "installed"})
        return 0
    except Exception as failure:
        _failed(stdout, None, "failed", f"Le téléchargement de la reconnaissance vocale a échoué : {failure}")
        return 1
