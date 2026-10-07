"""The project's names in a phrase heard: which were said, which were misheard.

A transcriber does not know « Lead Pluck » or « Valhalla Supermassive »;
favouring those words in its search changed nothing on the test set (S25).
What helps is after the fact: where the phrase names something — after
« piste », « pattern », « bus », or a word the model capitalised — the words
are compared with the project's names. Said exactly: kept. Close to one:
the nearest is proposed, never put in its place. Close to none: said as
unknown. Either way the phrase is doubtful, and waits for the person.
"""

from __future__ import annotations

import re
import unicodedata
from dataclasses import dataclass
from difflib import SequenceMatcher

# Words after which a name is expected.
LEADS = {"piste", "pistes", "pattern", "patterns", "bus", "plugin"}

# Words that end a name: French function words, and those of the commands.
STOPS = {
    "a", "à", "au", "aux", "avec", "dans", "de", "des", "du", "en", "et", "jusqu'à", "la", "le", "les",
    "pendant", "pour", "sur", "un", "une", "vers", "depuis", "puis", "qui", "que", "sous", "avant",
    "après", "entre", "par", "mais", "ou", "son", "sa", "ses",
}  # fmt: skip

# Ordinals and numbers: « la piste quatre » names a track by its rank.
NUMBERS = {
    "un", "une", "deux", "trois", "quatre", "cinq", "six", "sept", "huit", "neuf", "dix", "onze", "douze",
    "premier", "première", "dernier", "dernière", "nouvelle", "nouveau",
}  # fmt: skip

# Music words a transcriber capitalises (« Fa dièse », « BPM »): not names.
MUSIC = {
    "do", "ré", "re", "mi", "fa", "sol", "la", "si", "dièse", "bémol", "mineur", "majeur", "bpm", "db",
    "hz", "hertz", "décibels", "décibel", "kick", "snare", "clap", "reverb", "delay", "drop", "solo",
    "mute", "master", "sidechain", "mineure", "majeure", "octave", "octaves", "demi", "tons", "ton",
}  # fmt: skip

CLOSE = 0.5  # a name at least this close is proposed


def key(text: str) -> str:
    """A name as compared: no case, no accent, no space, no punctuation."""
    text = unicodedata.normalize("NFKD", text.lower())
    text = "".join(c for c in text if not unicodedata.combining(c))
    return re.sub(r"[^a-z0-9]", "", text)


def bare(word: str) -> str:
    return word.strip(".,;:!?«»\"'()").lower()


@dataclass(frozen=True)
class Named:
    heard: str  # the words as heard
    name: str | None  # the project's name it is, or the nearest
    exact: bool
    closeness: float
    at: int  # the index of its first word


def nearest(heard: str, names: list[str]) -> tuple[str | None, float]:
    wanted = key(heard)
    best, score = None, 0.0
    for name in names:
        candidate = key(name)
        if not candidate or not wanted:
            continue
        ratio = SequenceMatcher(None, wanted, candidate).ratio()
        if ratio > score:
            best, score = name, ratio
    return best, score


def spans(words: list[str]) -> list[tuple[int, int]]:
    """Where a name may be: after a lead word, or a run of words the model
    capitalised past the first; three words at most, up to a stop word."""
    found: list[tuple[int, int]] = []
    index = 0
    while index < len(words):
        word = bare(words[index])
        lead = word in LEADS and index + 1 < len(words)
        # « le volume de X », « le volume du X »: a name follows the article.
        if word == "volume" and index + 2 < len(words) and bare(words[index + 1]) in {"de", "du", "des"}:
            index += 1
            lead = True
        capital = index > 0 and words[index][:1].isupper() and word not in STOPS
        if lead or capital:
            begin = index + 1 if lead else index
            end = begin
            while end < len(words) and end - begin < 3 and bare(words[end]) not in STOPS:
                end += 1
                if words[end - 1].rstrip()[-1:] in ".,;:!?":
                    break
            if end > begin:
                found.append((begin, end))
                index = end
                continue
        index += 1
    return found


def find(words: list[str], names: list[str]) -> list[Named]:
    """Each place a name is expected, and what it is."""
    named = []
    for begin, end in spans(words):
        heard = " ".join(bare(w) for w in words[begin:end])
        if all(w in NUMBERS or w.isdigit() for w in heard.split()):
            continue  # a rank, not a name
        if not any(n for n in names if key(n) == key(heard)) and all(w in MUSIC for w in heard.split()):
            continue  # a note, a key, a unit
        # The longest run that is a name exactly, from its first word.
        exact = None
        for stop in range(end, begin, -1):
            candidate = " ".join(bare(w) for w in words[begin:stop])
            match = [n for n in names if key(n) == key(candidate)]
            if match:
                exact = (match[0], candidate)
                break
        if not exact:
            # Part of one name only, word for word: « le pad » for « Pad Ambient ».
            said = {key(w) for w in heard.split()}
            holders = [n for n in names if said and said <= {key(w) for w in n.split()}]
            if len(holders) == 1:
                exact = (holders[0], heard)
        if exact:
            named.append(Named(exact[1], exact[0], True, 1.0, begin))
            continue
        name, score = nearest(heard, names)
        named.append(Named(heard, name if score >= CLOSE else None, False, round(score, 2), begin))
    return named
