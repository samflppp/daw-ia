"""Standard MIDI files, read and written. Standard library only.

Enough of the format for what FL Studio exports: types 0 and 1, running status,
note on and off, tempo, time signature, track names. Everything else is skipped
by its declared length, which is what the format asks of a reader.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

DRUM_CHANNEL = 9  # channel 10, counted from one


@dataclass(frozen=True)
class MidiNote:
    pitch: int
    velocity: int
    start: int  # ticks
    end: int  # ticks
    channel: int


@dataclass
class MidiTrack:
    name: str = ""
    notes: list[MidiNote] = field(default_factory=list)


@dataclass
class MidiFile:
    ticks_per_beat: int
    tracks: list[MidiTrack]
    tempo_bpm: float | None = None
    numerator: int = 4
    denominator: int = 4


class MidiError(ValueError):
    pass


def _variable(data: bytes, at: int) -> tuple[int, int]:
    value = 0
    for _ in range(4):
        if at >= len(data):
            raise MidiError("truncated variable-length number")
        byte = data[at]
        at += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, at
    raise MidiError("variable-length number longer than four bytes")


def _read_track(data: bytes, into: MidiFile) -> MidiTrack:
    track = MidiTrack()
    at = 0
    now = 0
    status = 0
    sounding: dict[tuple[int, int], list[tuple[int, int]]] = {}

    while at < len(data):
        delta, at = _variable(data, at)
        now += delta
        if at >= len(data):
            raise MidiError("event without a status")

        if data[at] & 0x80:
            status = data[at]
            at += 1
        elif status == 0:
            raise MidiError("running status before any status")

        if status == 0xFF:
            kind = data[at]
            length, at = _variable(data, at + 1)
            body = data[at : at + length]
            at += length
            if kind == 0x03 and not track.name:
                track.name = body.decode("latin-1").strip()
            elif kind == 0x51 and length == 3 and into.tempo_bpm is None:
                into.tempo_bpm = 60_000_000 / int.from_bytes(body, "big")
            elif kind == 0x58 and length >= 2:
                into.numerator, into.denominator = body[0], 2 ** body[1]
            elif kind == 0x2F:
                break
            status = 0  # a meta event cancels running status
            continue

        if status in (0xF0, 0xF7):
            length, at = _variable(data, at)
            at += length
            status = 0
            continue

        kind = status & 0xF0
        channel = status & 0x0F
        size = 1 if kind in (0xC0, 0xD0) else 2
        if at + size > len(data):
            raise MidiError("truncated channel event")
        first = data[at]
        second = data[at + 1] if size == 2 else 0
        at += size

        if kind == 0x90 and second > 0:
            sounding.setdefault((channel, first), []).append((now, second))
        elif kind == 0x80 or (kind == 0x90 and second == 0):
            started = sounding.get((channel, first))
            if started:
                start, velocity = started.pop(0)
                if now > start:
                    track.notes.append(MidiNote(first, velocity, start, now, channel))

    # Notes never released end with the track.
    for (channel, pitch), starts in sounding.items():
        for start, velocity in starts:
            if now > start:
                track.notes.append(MidiNote(pitch, velocity, start, now, channel))

    track.notes.sort(key=lambda note: (note.start, note.pitch))
    return track


def read(data: bytes) -> MidiFile:
    if data[:4] != b"MThd" or len(data) < 14:
        raise MidiError("not a MIDI file")
    length = struct.unpack(">I", data[4:8])[0]
    _, count, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise MidiError("SMPTE time division is not supported")

    song = MidiFile(ticks_per_beat=division, tracks=[])
    at = 8 + length
    for _ in range(count):
        if data[at : at + 4] != b"MTrk":
            raise MidiError("missing track chunk")
        size = struct.unpack(">I", data[at + 4 : at + 8])[0]
        song.tracks.append(_read_track(data[at + 8 : at + 8 + size], song))
        at += 8 + size
    return song


# --- writing, for the tests and nothing else ---------------------------------


def _write_variable(value: int) -> bytes:
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append((value & 0x7F) | 0x80)
        value >>= 7
    return bytes(reversed(out))


def write(song: MidiFile) -> bytes:
    chunks = []
    for index, track in enumerate(song.tracks):
        events: list[tuple[int, int, bytes]] = []
        if index == 0 and song.tempo_bpm:
            tempo = round(60_000_000 / song.tempo_bpm).to_bytes(3, "big")
            events.append((0, 0, b"\xff\x51\x03" + tempo))
        if index == 0:
            power = song.denominator.bit_length() - 1
            events.append((0, 0, bytes([0xFF, 0x58, 0x04, song.numerator, power, 24, 8])))
        if track.name:
            name = track.name.encode("latin-1")
            events.append((0, 0, b"\xff\x03" + _write_variable(len(name)) + name))
        for note in track.notes:
            events.append((note.start, 2, bytes([0x90 | note.channel, note.pitch, note.velocity])))
            events.append((note.end, 1, bytes([0x80 | note.channel, note.pitch, 0])))
        events.sort(key=lambda event: (event[0], event[1]))

        body = bytearray()
        now = 0
        for when, _, payload in events:
            body += _write_variable(when - now) + payload
            now = when
        body += b"\x00\xff\x2f\x00"
        chunks.append(b"MTrk" + struct.pack(">I", len(body)) + bytes(body))

    header = struct.pack(">HHH", 1, len(song.tracks), song.ticks_per_beat)
    return b"MThd" + struct.pack(">I", 6) + header + b"".join(chunks)
