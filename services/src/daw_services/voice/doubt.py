"""What makes a phrase heard doubtful (S25). A doubtful phrase never acts.

It is shown with its uncertain words marked, and waits for the person to
correct it or to press Entrée. A sure phrase leaves on its own after a short
delay any key cancels. Each rule says why, in French, for the screen.

The thresholds were set on the test set (services/tests/voix, see
tools/voix_essai.py and docs/bilan-s25.md), not guessed: a word below
WORD_SURE was wrong more often than right there.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from daw_services.voice import RATE, Heard, Word
from daw_services.voice.names import Named, bare, find

WORD_SURE = 0.6  # a word's least sure piece, below which the word is uncertain
SPEECH_MIN = 0.4  # seconds of speech, under which nothing was really said
WORDS_MIN = 2  # a single word is not a command

# Function words, to tell French from another language: the transcriber does
# not say which language it heard.
FRENCH = {
    "le", "la", "les", "un", "une", "des", "du", "de", "sur", "en", "à", "au", "aux", "et", "est", "pour",
    "avec", "dans", "pendant", "mets", "met", "ajoute", "baisse", "monte", "coupe", "je", "tu", "on", "ce",
    "ça", "c'est", "il", "elle", "pas", "que", "qui", "plus", "moins", "fais", "passe", "piste", "règle",
    "mon", "ma", "mes", "son", "sa", "ses", "depuis", "vers", "d'une", "d'un", "l'", "j'ai", "nous", "vous",
}  # fmt: skip
OTHER = {
    "the", "a", "an", "of", "to", "up", "down", "please", "can", "you", "it", "my", "is", "and", "with",
    "turn", "make", "put", "this", "that", "little", "bit", "on", "off", "in", "for", "i", "me",
}  # fmt: skip


@dataclass
class Verdict:
    doubtful: bool
    reasons: list[str] = field(default_factory=list)  # French, one per rule that tripped
    uncertain: list[int] = field(default_factory=list)  # indexes of the words to mark
    names: list[Named] = field(default_factory=list)


def speech_seconds(samples: np.ndarray) -> float:
    """How long someone spoke: 20 ms frames above a floor set by the phrase's
    own quietest frames, and never under -45 dBFS."""
    frame = RATE // 50
    count = len(samples) // frame
    if count == 0:
        return 0.0
    energy = np.sqrt(np.mean(samples[: count * frame].reshape(count, frame) ** 2, axis=1) + 1e-12)
    levels = 20 * np.log10(energy)
    floor = max(np.percentile(levels, 10) + 12.0, -45.0)
    return float(np.sum(levels > floor)) * 0.02


def spoken_seconds(heard: Heard, samples: np.ndarray) -> float:
    """How long the words took, by their timestamps: a song behind does not
    lengthen it, as it lengthens what the level alone measures. The last
    word's own length is not given; 0.3 s is counted for it."""
    starts = [w.start for w in heard.words]
    if not starts:
        return 0.0
    if max(starts) == 0.0 and len(starts) > 1:
        return speech_seconds(samples)  # no timestamps: the level
    return max(starts) - min(starts) + 0.3


def judge(heard: Heard, samples: np.ndarray, names: list[str]) -> Verdict:
    verdict = Verdict(doubtful=False)

    def say(reason: str) -> None:
        verdict.doubtful = True
        if reason not in verdict.reasons:
            verdict.reasons.append(reason)

    words: list[Word] = heard.words
    spoken = spoken_seconds(heard, samples)
    if not heard.text.strip() or not words:
        say("je n'ai rien entendu")
        return verdict
    if spoken < SPEECH_MIN:
        say(f"trop peu de parole ({spoken:.1f} s)")
    if len(words) < WORDS_MIN:
        say("un seul mot")

    plain = [bare(w.text) for w in words]
    french = sum(1 for w in plain if w in FRENCH)
    other = sum(1 for w in plain if w in OTHER)
    if other > french:
        say("ce n'est pas du français")

    for index, word in enumerate(words):
        if word.confidence < WORD_SURE:
            verdict.uncertain.append(index)
    if verdict.uncertain:
        say("des mots incertains")

    verdict.names = find([w.text for w in words], names)
    for named in verdict.names:
        if named.exact:
            continue
        span = range(named.at, named.at + len(named.heard.split()))
        verdict.uncertain.extend(i for i in span if i not in verdict.uncertain)
        if named.name:
            say(f"« {named.heard} » : est-ce « {named.name} » ?")
        else:
            say(f"« {named.heard} » : aucun nom du projet")
    verdict.uncertain.sort()
    return verdict
