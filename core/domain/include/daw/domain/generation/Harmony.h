#pragma once

#include "daw/domain/generation/Constraints.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace daw::domain::generation
{

// The correctness layer. Rules, and nothing but rules: what is legal, never
// what is good. It is deterministic, explicable and never wrong -- a note it
// lets through is in the key, in the register and on the grid, whatever the
// style model thinks of it. The style model only ranks what comes out of here.
//
// A model that decided correctness would give the plausible-but-wrong; rules
// that decided style would give the correct-but-flat. Neither is allowed to do
// the other's job.

// The grid everything is counted on: sixteenths. A resolution coarser than
// that is a subset of it.
inline constexpr int stepsPerBeat = 4;
inline constexpr double stepBeats = 1.0 / stepsPerBeat;

[[nodiscard]] int stepsOf(Resolution resolution) noexcept;

// Semitone offsets of the seven degrees, from the tonic.
[[nodiscard]] const std::array<int, 7>& scaleOf(Mode mode) noexcept;

[[nodiscard]] bool inScale(int pitch, Key key) noexcept;

// A pitch as a count of scale degrees from the tonic of octave -1: the unit
// the style model measures intervals in. Nothing when the pitch is out of the
// key.
[[nodiscard]] std::optional<int> diatonicIndex(int pitch, Key key) noexcept;
[[nodiscard]] int pitchOfIndex(int index, Key key) noexcept;

// The rules that judge a note someone wrote. In minor they also accept the
// raised seventh of the harmonic minor (the G# of A minor), which drill and
// trap live on: it is counted as the seventh degree, the one it raises. Only
// what is read changes; the generator writes the natural scale, through
// diatonicIndex and legalPitches, as it always has. Mirrored by
// degree_index in theory.py.
[[nodiscard]] std::optional<int> degreeIndex(int pitch, Key key) noexcept;
[[nodiscard]] bool isLegal(int pitch, Key key) noexcept;

// Every pitch of the key between low and high, both included, ascending.
[[nodiscard]] std::vector<int> legalPitches(Key key, int low, int high);

// The pitch window of a role in a register, both ends included.
[[nodiscard]] std::pair<int, int> registerRange(Role role, Register reg) noexcept;

struct WeightedPitch
{
    int pitch{60};
    double weight{1.0}; // a duration, in beats
};

// Krumhansl-Kessler: the key whose profile correlates best with the pitch
// classes heard, weighted by how long they last. Nothing when nothing sounds.
// Ties go to the lowest tonic, minor first: the same notes always give the
// same key.
[[nodiscard]] std::optional<Key> detectKey(const std::vector<WeightedPitch>& pitches);

// A diatonic triad, by the degree of its root (0 = tonic).
struct Chord
{
    int root{0};

    friend bool operator==(const Chord& lhs, const Chord& rhs) = default;
};

// True for the triads that are neither major nor minor: vii in major, ii in
// minor. They are legal and never proposed as a chord of their own.
[[nodiscard]] bool isDiminished(Chord chord, Key key) noexcept;

// The three degrees of the triad, 0..6.
[[nodiscard]] std::array<int, 3> chordDegrees(Chord chord) noexcept;

[[nodiscard]] bool isChordTone(int pitch, Chord chord, Key key) noexcept;

// The triad that covers most of what sounds, by weight. Nothing when nothing
// in the key sounds.
[[nodiscard]] std::optional<Chord> detectChord(const std::vector<WeightedPitch>& pitches, Key key);

} // namespace daw::domain::generation
