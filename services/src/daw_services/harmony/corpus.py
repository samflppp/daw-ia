"""The corpus pipeline: MIDI files in, the style model's tables out.

    raw/*.mid  ->  clean  ->  annotate (key, tempo, role)  ->  count  ->  markov.json

What each step does, and what it refuses:

- clean: a file that does not read, a file seen before (same bytes, or the same
  notes), a track with no note, a track whose onsets are not on a sixteenth
  grid. Each refusal is counted and said in the report.
- annotate: corpus.csv first, the notes second. The key by Krumhansl-Kessler
  on the file's pitched tracks, the tempo from the file, the role from the
  drum channel, the polyphony and the register.
- count: every context the generator will ask about, in the spelling
  core/domain/generation/StyleModel.h writes down.

No transposition. The counts are in degrees and intervals from the tonic, so a
phrase in F#m and the same phrase in Am count as one thing already; transposed
copies would add nothing but weight to what is there.
"""

from __future__ import annotations

import csv
import hashlib
import json
import statistics
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from daw_services.harmony import midi
from daw_services.harmony.theory import Key, detect_chord, detect_key, diatonic_index, parse_key

FORMAT = "daw-ia.style"
VERSION = 1
STEPS_PER_BEAT = 4
MAX_GAP = 16
MAX_INTERVAL = 9
ROLES = ("melody", "bass", "chords", "rhythm")

# Past this share of onsets off the sixteenth grid, a track was played in, not
# drawn, and its rhythm says more about the hand than about the style.
OFF_GRID_TOLERANCE = 0.2


@dataclass
class Annotation:
    key: Key | None = None
    bpm: float | None = None
    roles: dict[str, str] = field(default_factory=dict)  # track name -> role


@dataclass
class Report:
    files: int = 0
    kept: int = 0
    tracks: dict[str, int] = field(default_factory=lambda: dict.fromkeys(ROLES, 0))
    events: dict[str, int] = field(default_factory=lambda: dict.fromkeys(ROLES, 0))
    refused: list[str] = field(default_factory=list)
    keys: dict[str, int] = field(default_factory=dict)
    key_sources: dict[str, int] = field(default_factory=lambda: {"csv": 0, "detected": 0})
    tempos: list[float] = field(default_factory=list)
    out_of_key: int = 0
    in_key: int = 0

    def to_dict(self) -> dict:
        return {
            "files": self.files,
            "kept": self.kept,
            "tracks": self.tracks,
            "events": self.events,
            "refused": self.refused,
            "keys": dict(sorted(self.keys.items())),
            "keySources": self.key_sources,
            "tempos": sorted(self.tempos),
            "outOfKey": self.out_of_key,
            "inKey": self.in_key,
        }


class Tables:
    """The counts of one role, in the style model's spelling."""

    def __init__(self) -> None:
        self.rhythm: dict[str, dict[int, float]] = defaultdict(lambda: defaultdict(float))
        self.duration: dict[str, dict[int, float]] = defaultdict(lambda: defaultdict(float))
        self.interval: dict[str, dict[int, float]] = defaultdict(lambda: defaultdict(float))
        self.degree: dict[str, dict[int, float]] = defaultdict(lambda: defaultdict(float))
        self.progression: dict[str, dict[int, float]] = defaultdict(lambda: defaultdict(float))
        self.velocities: dict[int, list[int]] = defaultdict(list)

    @staticmethod
    def suffixes(prefix: str, history: list[int]) -> list[str]:
        recent = history[-3:]
        return [prefix + ",".join(str(v) for v in recent[start:]) for start in range(len(recent) + 1)]

    def to_dict(self) -> dict:
        def table(counts: dict[str, dict[int, float]]) -> dict:
            return {
                context: {str(x): count for x, count in sorted(values.items())}
                for context, values in sorted(counts.items())
            }

        velocity = {}
        for position, values in sorted(self.velocities.items()):
            mean = statistics.fmean(values)
            deviation = statistics.pstdev(values) if len(values) > 1 else 8.0
            velocity[str(position)] = [round(mean, 3), round(max(deviation, 2.0), 3)]

        return {
            "rhythm": table(self.rhythm),
            "duration": table(self.duration),
            "interval": table(self.interval),
            "degree": table(self.degree),
            "progression": table(self.progression),
            "velocity": velocity,
        }


# --- annotation -----------------------------------------------------------------


def read_annotations(path: Path | None) -> dict[str, Annotation]:
    """corpus.csv: file, bpm, key, then 'track=role' pairs separated by ';'.

    file;bpm;key;roles
    Nuit.mid;142;F#m;Lead=melody;808=bass
    """
    if path is None or not path.exists():
        return {}

    out: dict[str, Annotation] = {}
    with path.open(encoding="utf-8-sig", newline="") as handle:
        for row in csv.reader(handle, delimiter=";"):
            if not row or row[0].strip().lower() in ("", "file", "fichier") or row[0].startswith("#"):
                continue
            annotation = Annotation()
            if len(row) > 1 and row[1].strip():
                annotation.bpm = float(row[1].replace(",", "."))
            if len(row) > 2 and row[2].strip():
                annotation.key = parse_key(row[2])
            for pair in row[3:]:
                if "=" in pair:
                    name, role = pair.split("=", 1)
                    if role.strip() in ROLES:
                        annotation.roles[name.strip().lower()] = role.strip()
            out[row[0].strip().lower()] = annotation
    return out


def classify(track: midi.MidiTrack) -> str:
    notes = track.notes
    if all(note.channel == midi.DRUM_CHANNEL for note in notes) or len({n.pitch for n in notes}) == 1:
        return "rhythm"

    name = track.name.lower()
    for words, role in (
        (("808", "bass", "basse", "sub"), "bass"),
        (("chord", "accord", "pad", "nappe"), "chords"),
        (("lead", "melod", "pluck", "bell"), "melody"),
    ):
        if any(word in name for word in words):
            return role

    starts: dict[int, int] = defaultdict(int)
    for note in notes:
        starts[note.start] += 1
    if max(starts.values()) >= 3:
        return "chords"
    pitches = sorted(note.pitch for note in notes)
    return "bass" if pitches[len(pitches) // 2] < 48 else "melody"


# --- counting --------------------------------------------------------------------


@dataclass(frozen=True)
class Event:
    step: int
    length: int
    pitch: int
    velocity: int


def line_of(track: midi.MidiTrack, step_ticks: float, role: str) -> list[Event]:
    """One event per onset: the highest pitch for a melody, the lowest for a bass."""
    by_step: dict[int, list[midi.MidiNote]] = defaultdict(list)
    for note in track.notes:
        by_step[round(note.start / step_ticks)].append(note)

    events = []
    for step in sorted(by_step):
        chosen = (
            min(by_step[step], key=lambda n: n.pitch)
            if role == "bass"
            else max(by_step[step], key=lambda n: n.pitch)
        )
        length = max(1, round((chosen.end - chosen.start) / step_ticks))
        events.append(Event(step, length, chosen.pitch, chosen.velocity))
    return events


def count_line(
    tables: Tables, events: list[Event], key: Key, bar_steps: int, report: Report, role: str
) -> None:
    gaps: list[int] = []
    intervals: list[int] = []
    previous: int | None = None

    for i, event in enumerate(events):
        position = event.step % bar_steps % 16
        tables.velocities[position].append(event.velocity)

        if i + 1 < len(events):
            gap = events[i + 1].step - event.step
            if gap > MAX_GAP:
                # A long rest ends the phrase: what comes after does not follow from it.
                gaps = []
            else:
                for context in Tables.suffixes(f"p{position}|", gaps):
                    tables.rhythm[context][gap] += 1
                tables.rhythm["|"][gap] += 1
                tables.duration[str(gap)][min(event.length, gap)] += 1
                gaps = (gaps + [gap])[-3:]
                report.events[role] += 1

        if role == "rhythm":
            continue

        index = diatonic_index(event.pitch, key)
        if index is None:
            report.out_of_key += 1
            continue
        report.in_key += 1

        tables.degree["s" if position % STEPS_PER_BEAT == 0 else "w"][index % 7] += 1
        if previous is not None:
            step = max(-MAX_INTERVAL, min(MAX_INTERVAL, index - previous))
            for context in Tables.suffixes("", intervals):
                tables.interval[context][step] += 1
            intervals = (intervals + [step])[-3:]
        previous = index


def count_chords(
    tables: Tables, track: midi.MidiTrack, key: Key, step_ticks: float, bar_steps: int, report: Report
) -> None:
    onsets = sorted({round(note.start / step_ticks) for note in track.notes})
    velocities = {round(n.start / step_ticks): n.velocity for n in track.notes}
    events = [Event(step, 1, 0, velocities[step]) for step in onsets]
    for event in events:
        tables.velocities[event.step % bar_steps % 16].append(event.velocity)

    gaps: list[int] = []
    for i in range(len(events) - 1):
        gap = events[i + 1].step - events[i].step
        position = events[i].step % bar_steps % 16
        if gap > MAX_GAP:
            gaps = []
            continue
        for context in Tables.suffixes(f"p{position}|", gaps):
            tables.rhythm[context][gap] += 1
        tables.rhythm["|"][gap] += 1
        tables.duration[str(gap)][gap] += 1
        gaps = (gaps + [gap])[-3:]
        report.events["chords"] += 1

    # One chord per bar, read with the generator's own rule.
    bar_ticks = step_ticks * bar_steps
    last_bar = max(note.end for note in track.notes) / bar_ticks
    roots: list[int] = []
    bar = 0
    while bar < last_bar:
        start, end = bar * bar_ticks, (bar + 1) * bar_ticks
        heard = [
            (note.pitch, min(end, note.end) - max(start, note.start))
            for note in track.notes
            if min(end, note.end) > max(start, note.start)
        ]
        root = detect_chord(heard, key)
        if root is not None:
            for context in Tables.suffixes("", roots):
                tables.progression[context][root] += 1
            roots = (roots + [root])[-3:]
        bar += 1


# --- the pipeline ------------------------------------------------------------------


def off_grid_share(track: midi.MidiTrack, step_ticks: float) -> float:
    off = sum(1 for n in track.notes if abs(n.start / step_ticks - round(n.start / step_ticks)) > 0.25)
    return off / len(track.notes)


def build(raw: Path, annotations_path: Path | None = None) -> tuple[dict, Report]:
    annotations = read_annotations(annotations_path)
    report = Report()
    tables = {role: Tables() for role in ROLES}
    seen_bytes: set[str] = set()
    seen_notes: set[str] = set()

    for path in sorted(raw.rglob("*")):
        if path.suffix.lower() not in (".mid", ".midi"):
            continue
        report.files += 1
        data = path.read_bytes()

        digest = hashlib.blake2b(data, digest_size=16).hexdigest()
        if digest in seen_bytes:
            report.refused.append(f"{path.name}: doublon")
            continue
        seen_bytes.add(digest)

        try:
            song = midi.read(data)
        except midi.MidiError as error:
            report.refused.append(f"{path.name}: illisible ({error})")
            continue

        content = hashlib.blake2b(
            repr(sorted((n.pitch, n.start, n.end) for t in song.tracks for n in t.notes)).encode(),
            digest_size=16,
        ).hexdigest()
        if content in seen_notes:
            report.refused.append(f"{path.name}: mêmes notes qu'un autre fichier")
            continue
        seen_notes.add(content)

        annotation = annotations.get(path.name.lower(), Annotation())
        step_ticks = song.ticks_per_beat / STEPS_PER_BEAT
        bar_steps = round(song.numerator * 4 / song.denominator * STEPS_PER_BEAT)

        tracks = []
        for track in song.tracks:
            if not track.notes:
                continue
            if off_grid_share(track, step_ticks) > OFF_GRID_TOLERANCE:
                report.refused.append(
                    f"{path.name} / {track.name or 'sans nom'}: hors grille (joué, pas dessiné)"
                )
                continue
            role = annotation.roles.get(track.name.lower()) or classify(track)
            tracks.append((track, role))
        if not tracks:
            report.refused.append(f"{path.name}: aucune piste utilisable")
            continue

        key = annotation.key
        if key is not None:
            report.key_sources["csv"] += 1
        else:
            pitched = [
                (n.pitch, (n.end - n.start) / song.ticks_per_beat)
                for track, role in tracks
                if role != "rhythm"
                for n in track.notes
            ]
            key = detect_key(pitched)
            report.key_sources["detected"] += 1
        if key is None:
            key = Key(9, True)

        report.kept += 1
        report.keys[str(key)] = report.keys.get(str(key), 0) + 1
        bpm = annotation.bpm or song.tempo_bpm
        if bpm:
            report.tempos.append(round(bpm, 2))

        for track, role in tracks:
            report.tracks[role] += 1
            if role == "chords":
                count_chords(tables[role], track, key, step_ticks, bar_steps, report)
            else:
                count_line(tables[role], line_of(track, step_ticks, role), key, bar_steps, report, role)

    model = {
        "format": FORMAT,
        "version": VERSION,
        "origin": "corpus",
        "stats": report.to_dict(),
        "roles": {role: tables[role].to_dict() for role in ROLES},
    }
    return model, report


def describe(report: Report) -> str:
    lines = [
        f"fichiers : {report.files}, gardés : {report.kept}, refusés : {len(report.refused)}",
        "pistes par rôle : " + ", ".join(f"{role} {count}" for role, count in report.tracks.items()),
        "événements comptés : " + ", ".join(f"{role} {count}" for role, count in report.events.items()),
        "tonalités : " + ", ".join(f"{key} {count}" for key, count in sorted(report.keys.items())),
        f"tonalité lue dans le CSV : {report.key_sources['csv']}, "
        f"détectée : {report.key_sources['detected']}",
    ]
    total = report.in_key + report.out_of_key
    if total:
        share = 100 * report.out_of_key / total
        lines.append(f"notes hors de la gamme détectée : {report.out_of_key} sur {total} ({share:.1f} %)")
    if report.tempos:
        lines.append(f"tempos : {min(report.tempos)} à {max(report.tempos)} BPM")
    lines += [f"  refusé : {reason}" for reason in report.refused]
    return "\n".join(lines)


def write(model: dict, out: Path) -> None:
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(model, ensure_ascii=False, indent=1, sort_keys=True), encoding="utf-8")
