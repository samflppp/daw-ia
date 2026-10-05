#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/direction/Reading.h"
#include "daw/domain/generation/Constraints.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace daw::domain::direction
{

// The direction of a project (S22): one or more references, read once into
// numbers, and what the person corrected by hand. It lives in the project —
// it reopens with it — and is changed by one verb, direction.set. A
// reference is named by its digest and its name, never by a path of this
// machine; its audio is not kept, only its reading.
//
// What reads it: the mix (its target), the generation (key, density), the
// copilot (the project's summary). An empty direction changes nothing: the
// software of S21, byte for byte.

struct Reference
{
    Reading reading;    // the digest and the name are the reading's
    double weight{1.0}; // > 0: how much this reference counts against the others

    friend bool operator==(const Reference& lhs, const Reference& rhs) = default;
};

// What the person set by hand, over whatever the references say.
struct Corrections
{
    std::optional<double> bpm;
    std::optional<generation::Key> key;

    friend bool operator==(const Corrections& lhs, const Corrections& rhs) = default;
};

struct Direction
{
    std::vector<Reference> references;
    Corrections corrections;
    double amount{0.5}; // 0 the project's own choices, 1 all the way to the references

    [[nodiscard]] bool empty() const noexcept
    {
        return references.empty() && !corrections.bpm.has_value() && !corrections.key.has_value();
    }

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Direction> fromValue(const Value& value);

    friend bool operator==(const Direction& lhs, const Direction& rhs) = default;
};

// What the references say together. Continuous values (the shape of the
// spectrum, the dynamics, the width, the balance and the activity of each
// stem) are weighted means. A tempo or a key is never averaged: two
// references that disagree leave it empty and say so, in a sentence the
// panel shows, until the person chooses.
struct Combined
{
    std::optional<double> bpm;
    std::optional<generation::Key> key;
    bool bpmCorrected{false};
    bool keyCorrected{false};

    // A key the references could not settle: the candidates offered.
    std::vector<generation::Key> keyCandidates;

    std::vector<std::string> contradictions; // French, one sentence each

    std::optional<std::array<double, mix::bandCount>> tilt;
    std::optional<double> crestDb;
    std::optional<double> sideShare;
    std::map<std::string, double> balanceDb;   // per stem
    std::map<std::string, double> activeShare; // per stem

    // The sections of the reference that counts most, and the tempo they
    // were cut at: that reference's own reading, never a correction or a
    // mean — the cuts fall on its bars and no others.
    std::vector<Section> sections;
    std::optional<double> sectionsBpm;
    double amount{0.0};
};

[[nodiscard]] Combined combine(const Direction& direction);

// A section on the project's grid (S23): where it would fall if the project
// started where the reference starts, in beats of the project. A bar of the
// reference is a bar: eight of them are eight at any tempo of the project.
// The reading cuts sections on whole bars of four beats from the start of
// the file, so the bounds are whole bars of the reference; the last one,
// cut short by the end of the file, is rounded to the nearest.
struct GridSection
{
    double fromBeats{0.0};
    double toBeats{0.0};
    char label{'A'};

    friend bool operator==(const GridSection& lhs, const GridSection& rhs) = default;
};

// Empty when the sections were not cut on a tempo (the reading found none:
// they are then two-second blocks, with no bar to stand on).
[[nodiscard]] std::vector<GridSection> onGrid(const Combined& combined);

// The bars a section covers, counted from 1 as the ruler counts them: the
// bar its first beat falls in, and the last bar it reaches into.
[[nodiscard]] int firstBar(const GridSection& section, double beatsPerBar);
[[nodiscard]] int lastBar(const GridSection& section, double beatsPerBar);

// Two tempos are the same pulse within this share.
inline constexpr double sameTempo = 0.03;

} // namespace daw::domain::direction
