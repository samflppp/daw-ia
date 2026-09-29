"""Keys, scales, chords: the same rules as core/domain/generation/Harmony.cpp.

Written twice on purpose. The corpus is counted here and generated from there,
and the two must agree on what a degree is, on which key a phrase is in and on
which chord a bar holds, or the counts describe something the generator never
asks about. tests/test_corpus.py pins the cases both sides are tested on.
"""

from __future__ import annotations

from dataclasses import dataclass

MAJOR = (0, 2, 4, 5, 7, 9, 11)
MINOR = (0, 2, 3, 5, 7, 8, 10)

# Krumhansl and Kessler, 1982.
MAJOR_PROFILE = (6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88)
MINOR_PROFILE = (6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17)

# A near tie between a major key and its relative minor goes to minor.
MINOR_BIAS = 0.05

NAMES = ("C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B")
LETTERS = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


@dataclass(frozen=True)
class Key:
    tonic: int
    minor: bool

    @property
    def scale(self) -> tuple[int, ...]:
        return MINOR if self.minor else MAJOR

    def __str__(self) -> str:
        return NAMES[self.tonic] + ("m" if self.minor else "")


def parse_key(text: str) -> Key | None:
    """'Am', 'F#m', 'Bb', 'C' -- the words the piano roll's field reads."""
    word = text.strip().lower()
    if not word or word[0] not in LETTERS:
        return None
    tonic = LETTERS[word[0]]
    rest = word[1:]
    if rest.startswith("#"):
        tonic, rest = tonic + 1, rest[1:]
    elif rest.startswith("b"):
        tonic, rest = tonic + 11, rest[1:]
    if rest in ("", "maj"):
        return Key(tonic % 12, False)
    if rest in ("m", "min"):
        return Key(tonic % 12, True)
    return None


def diatonic_index(pitch: int, key: Key) -> int | None:
    octave, within = divmod(pitch - key.tonic, 12)
    if within not in key.scale:
        return None
    return octave * 7 + key.scale.index(within)


def degree_index(pitch: int, key: Key) -> int | None:
    """The rules that judge a written note: the scale, plus in minor the raised
    seventh of the harmonic minor (the G# of A minor), counted as the seventh
    degree. The generator writes the natural scale. Mirrors degreeIndex in
    Harmony.cpp."""
    index = diatonic_index(pitch, key)
    if index is not None:
        return index
    if key.minor and (pitch - key.tonic) % 12 == 11:
        return diatonic_index(pitch - 1, key)
    return None


def _correlation(heard: list[float], profile: tuple[float, ...], tonic: int) -> float:
    mean_heard = sum(heard) / 12
    mean_profile = sum(profile) / 12
    covariance = variance_heard = variance_profile = 0.0
    for i in range(12):
        h = heard[(i + tonic) % 12] - mean_heard
        p = profile[i] - mean_profile
        covariance += h * p
        variance_heard += h * h
        variance_profile += p * p
    if variance_heard <= 0 or variance_profile <= 0:
        return 0.0
    return covariance / (variance_heard * variance_profile) ** 0.5


def detect_key(pitches: list[tuple[int, float]]) -> Key | None:
    """(pitch, weight) pairs, the weight a duration. Ties go to the lowest tonic, minor first."""
    heard = [0.0] * 12
    for pitch, weight in pitches:
        heard[pitch % 12] += weight
    if sum(heard) <= 0:
        return None

    best: Key | None = None
    best_score = -2.0
    for tonic in range(12):
        for minor in (True, False):
            score = _correlation(heard, MINOR_PROFILE if minor else MAJOR_PROFILE, tonic)
            if minor:
                score += MINOR_BIAS
            if score > best_score + 1e-9:
                best_score, best = score, Key(tonic, minor)
    return best


def is_diminished(root: int, key: Key) -> bool:
    return (not key.minor and root % 7 == 6) or (key.minor and root % 7 == 1)


def detect_chord(pitches: list[tuple[int, float]], key: Key) -> int | None:
    """The root degree of the triad that covers most of what sounds."""
    by_degree = [0.0] * 7
    for pitch, weight in pitches:
        index = diatonic_index(pitch, key)
        if index is not None:
            by_degree[index % 7] += weight
    if sum(by_degree) <= 0:
        return None

    best: int | None = None
    best_score = 0.0
    for root in range(7):
        if is_diminished(root, key):
            continue
        score = by_degree[root] * 1.2 + by_degree[(root + 2) % 7] + by_degree[(root + 4) % 7]
        if score > best_score + 1e-9:
            best_score, best = score, root
    return best
