#pragma once

#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/StyleModel.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::domain::generation
{

// Reworking notes that exist (S16), rather than writing new ones.
//
// A layer beside the generator, not inside it: Harmony, the form, the style
// and the contract are used as they are. The notes of the zone are the
// source; each transformation says what it keeps and what it changes, and a
// test measures exactly that on the notes before and after.
//
//   keepRhythm   same attacks, lengths and velocities; the pitches are those
//                the generator draws for the zone, in order. All in the key.
//   keepPitches  the same pitches in the same order; the attacks are those the
//                generator draws for the zone, as many as there are notes.
//   variation    one change, as an A' of the form: a note moved by one step,
//                a velocity changed, or the last note closed on the tonic or
//                opened on the fifth.
//   darker       same rhythm; each pitch one step darker: the major degrees
//                that have a minor counterpart are lowered, and in a minor key
//                each note goes one degree down. All in the (darker) key.
//   brighter     the opposite.
//   busier       same pitches in the same order; each note long enough is
//                split in two, so more attacks.
//   calmer       same pitches in the same order, fewer of them: the notes off
//                the beat leave, and the one before is held through.
//   humanize     same pitches, same order; every attack moves by at most a
//                sixty-fourth of a beat, every velocity by at most 8.
//
// A pitch the person wrote out of the key is left alone by the transformations
// that keep pitches: the rules decide what the generator writes, not what the
// person did.
enum class Transform : std::uint8_t
{
    keepRhythm,
    keepPitches,
    variation,
    darker,
    brighter,
    busier,
    calmer,
    humanize
};

// What the transformation does, in a musician's words, for the sentence under
// the grey notes: "même rythme, notes plus sombres".
[[nodiscard]] std::string_view describe(Transform transform) noexcept;

// The name the remote reader and the details use: "keep_rhythm", "darker"...
[[nodiscard]] std::string_view nameOf(Transform transform) noexcept;
[[nodiscard]] std::optional<Transform> transformNamed(std::string_view name) noexcept;

// The local reading: the words of a prompt that ask for a transformation, and
// which words were taken for it. Nothing when none is asked for.
struct TransformReading
{
    Transform transform{Transform::keepRhythm};
    std::vector<std::string> words; // the words it was read from, as typed
};
[[nodiscard]] std::optional<TransformReading> readTransform(std::string_view text);

// The notes of a row that start in the zone, in time order, as the source.
[[nodiscard]] std::vector<GhostNote> sourceOf(const Context& context);

// One variant of the transformation. Deterministic: the same inputs give the
// same notes on any machine. Empty when there is nothing to rework.
[[nodiscard]] std::vector<GhostNote> transform(Transform transform,
                                               const std::vector<GhostNote>& source,
                                               const Context& context,
                                               const ResolvedConstraints& constraints,
                                               const StyleModel& model,
                                               int variant);

} // namespace daw::domain::generation
