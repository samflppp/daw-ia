"""The push-to-talk's transcription (S25): a phrase spoken, its text, how sure.

The DAW holds the microphone; this side never opens one. While the key is
held the DAW gathers the samples, and on release hands them over: 16 kHz,
mono, 16 bits. What comes back is the text, each word with how sure the model
is of it, and whether the phrase is doubtful — a doubtful phrase never acts,
it waits for the person (see doubt.py).

Two transcribers behind one interface:

  parakeet  NVIDIA Parakeet TDT 0.6B v3, int8, through sherpa-onnx, on the
            CPU (the `voix` extra; weights downloaded on first use).
  replay    what Parakeet heard on the test set, recorded once and replayed
            by the audio's digest: the CI's transcriber. It proves the
            protocol and the doubt, never the model.

Decided with the founder on 7 October 2026 (docs/bilan-s25.md). The voice is
kept nowhere: the samples live in memory for one request.
"""

from __future__ import annotations

import base64
import hashlib
from dataclasses import dataclass, field
from typing import Protocol

import numpy as np

RATE = 16000


@dataclass(frozen=True)
class Word:
    text: str
    confidence: float  # 0..1: the least sure of its pieces
    start: float = 0.0  # seconds into the phrase
    heard: str = ""  # what the transcriber wrote, when a homophone was put right


@dataclass(frozen=True)
class Heard:
    """What a transcriber heard: the text, word by word."""

    text: str
    words: list[Word] = field(default_factory=list)


class Transcriber(Protocol):
    name: str

    def transcribe(self, samples: np.ndarray) -> Heard:
        """`samples`: float32, 16 kHz, mono, -1..1."""
        ...


def samples_of(pcm16: bytes) -> np.ndarray:
    """16-bit little-endian PCM to float32."""
    return np.frombuffer(pcm16, dtype="<i2").astype(np.float32) / 32768.0


def decode(encoded: str) -> np.ndarray:
    return samples_of(base64.b64decode(encoded))


def digest(samples: np.ndarray) -> str:
    """The digest of a phrase's samples as 16-bit PCM: the replay's key."""
    pcm = np.clip(np.round(samples * 32768.0), -32768, 32767).astype("<i2").tobytes()
    return hashlib.sha256(pcm).hexdigest()


def words_of(tokens: list[str], log_probs: list[float], times: list[float]) -> list[Word]:
    """Words from a transducer's pieces: a piece that starts with a space
    starts a word; punctuation stays with the word before it."""
    words: list[tuple[str, float, float]] = []
    starting = True
    for index, piece in enumerate(tokens):
        probability = float(np.exp(log_probs[index])) if index < len(log_probs) else 1.0
        at = times[index] if index < len(times) else 0.0
        if piece.startswith(" "):
            starting = True
        text = piece.strip()
        if not text:
            continue
        punctuation = not text.strip(".,;:!?")
        if (starting and not punctuation) or not words:
            words.append((text, probability, at))
        else:
            previous, least, start = words[-1]
            words[-1] = (previous + text, least if punctuation else min(least, probability), start)
        starting = False
    return [Word(text, round(confidence, 3), round(start, 2)) for text, confidence, start in words]


# « Mets » and « Mais » sound the same; the transcriber writes the second more
# often (6 phrases in 36 of the test set). At the head of a phrase, before an
# article, only the imperative makes a command: it is put right, the word
# keeps what was heard, and the screen shows it.
DETERMINERS = {"le", "la", "les", "un", "une", "du", "des", "l'", "ma", "mon", "mes", "ce", "cette", "ces"}


def homophones(heard: Heard) -> Heard:
    words = list(heard.words)
    if len(words) >= 2 and words[0].text.lower().strip(",") == "mais":
        following = words[1].text.lower()
        if following in DETERMINERS or following.startswith("l'"):
            first = words[0]
            words[0] = Word("Mets", first.confidence, first.start, first.text)
            text = heard.text
            text = "Mets" + text[len(first.text) :] if text.startswith(first.text) else text
            return Heard(text, words)
    return heard
