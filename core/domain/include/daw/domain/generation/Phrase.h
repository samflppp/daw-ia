#pragma once

#include "daw/domain/generation/Constraints.h"

#include <string>

namespace daw::domain::generation
{

// The short sentence under the grey notes, in a musician's words: "quatre
// mesures de mélodie en la mineur, dans ton style".
//
// It is the screen's default and the technical line of S14-S15 is its detail,
// folded away. It is built here, from what the generator resolved, and never
// from what an interpreter said it understood: the sentence describes the
// notes on screen, so it stays true offline and when a remote model is wrong.
//
// It names the key and the role always, the length always, and the rest only
// when the person asked for it: a resolution, a register or a form the
// generator deduced is the generator's business, not something to read.
struct PhraseInput
{
    const ResolvedConstraints& constraints;
    double lengthBeats{16.0};
    double beatsPerBar{4.0};

    // The part of the style learned from the person's projects, 0 to 1.
    double learnedShare{0.0};
};

// From this share on, the sentence says "dans ton style".
inline constexpr double styleShareSpoken = 0.25;

[[nodiscard]] std::string phrase(const PhraseInput& input);

// "quatre mesures", "une mesure", "six temps", "24 mesures".
[[nodiscard]] std::string lengthWords(double lengthBeats, double beatsPerBar);

} // namespace daw::domain::generation
