#pragma once

#include "daw/domain/kit/Features.h"

#include <optional>
#include <string>
#include <vector>

namespace daw::domain::kit
{

// The kit chosen from a library (S24), by rules alone — decided with the
// founder on 6 October 2026: the constraints are measured and checked, a
// model would add nothing the machine can verify. The same library, the same
// tonic and the same axes give the same kit.

// One sample of the library, measured, with its role.
struct Sample
{
    std::string path;
    Role role{Role::kick};
    Features features;
};

// The continuous axes, each from -2 to +2, measured against the samples of
// the same role in the library (each axis a mean of z-scores):
//   bright   dark ↔ bright: the centroid, the share above 5 kHz
//   ample    dry ↔ ample: the tail, the width
//   dirty    clean ↔ saturated: the crest, low for saturated, and the share
//            above 5 kHz — an approximation, said as one
struct Axes
{
    double bright{0.0};
    double ample{0.0};
    double dirty{0.0};

    friend bool operator==(const Axes&, const Axes&) = default;
};

// The axes of each sample of the library, in its order.
[[nodiscard]] std::vector<Axes> axesOf(const std::vector<Sample>& library);

// What « going together » is, in numbers.
inline constexpr double tunedCents = 15.0;        // an 808 on the tonic, or its fifth
inline constexpr double overlapCorrelation = 0.5; // a kick's low end against the 808's, at most
inline constexpr double lowApart = 1.25;    // the kick's low peak, a quarter off the 808's pitch at least
inline constexpr double colourApart = 0.75; // two elements of the kit on each axis, at most

struct Pick
{
    Role role{Role::kick};
    std::string path;
    Axes axes;
    std::vector<std::string> reasons; // French, each with the number it rests on
};

struct Kit
{
    std::vector<Pick> picks;
    std::vector<std::string> missing; // what could not be chosen, and why
};

// The kit: the 808 first, then the kick, the snare (a clap when there is
// none), the closed and the open hats, a percussion. Each the nearest to the
// axes wanted among the samples of its role that keep every constraint with
// what is already chosen; a tie goes to the first path. `tonic` is the
// project's key (0 is C); without one, the 808 is not tuned, and it is said.
[[nodiscard]] Kit choose(const std::vector<Sample>& library, std::optional<int> tonic, const Axes& wanted);

// A pitch class said in French: « do », « do# », … « si ».
[[nodiscard]] std::string noteName(int pitchClass);

// How far two low profiles go together: Pearson's correlation of their levels.
[[nodiscard]] double lowCorrelation(const Features& first, const Features& second);

} // namespace daw::domain::kit
