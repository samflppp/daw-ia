#pragma once

#include "daw/ui/Tokens.h"

#include <juce_graphics/juce_graphics.h>

namespace daw::ui
{

// The two typefaces of the interface, embedded in the binary.
//
// Embedded rather than named: neither Inter nor JetBrains Mono is installed on
// a stock Windows, and a family JUCE cannot resolve falls back to the system
// sans without saying so. A design that looks right only on the machine that
// drew it is not a design system.
//
// The split is semantic, not decorative. Inter carries language — names,
// labels, messages. JetBrains Mono carries values the user reads as numbers and
// compares to each other: decibels, bars, tempo, command types. Its figures are
// of equal width, so a fader sweep does not make its own readout jitter.
//
// Hygiene rule 1 holds here too: no size and no weight is written in this file.
// Callers name a token path, never a number.
class Typography
{
public:
    explicit Typography(const Tokens& tokens);

    // sizePath and weightPath are token paths, for example "font.size.body"
    // and "font.weight.medium".
    [[nodiscard]] juce::Font sans(juce::StringRef sizePath, juce::StringRef weightPath) const;
    [[nodiscard]] juce::Font mono(juce::StringRef sizePath, juce::StringRef weightPath) const;

    // Uppercase micro-labels — panel headers — are the one place where letters
    // are spaced apart, so the token that does it is named rather than passed.
    [[nodiscard]] juce::Font caps(juce::StringRef sizePath) const;

private:
    [[nodiscard]] juce::Font
    build(const juce::Typeface::Ptr& typeface, juce::StringRef sizePath, juce::StringRef trackingPath) const;

    [[nodiscard]] const juce::Typeface::Ptr& sansFor(juce::StringRef weightPath) const;
    [[nodiscard]] const juce::Typeface::Ptr& monoFor(juce::StringRef weightPath) const;

    const Tokens& tokens_;

    juce::Typeface::Ptr sansRegular_;
    juce::Typeface::Ptr sansMedium_;
    juce::Typeface::Ptr sansSemiBold_;
    juce::Typeface::Ptr monoRegular_;
    juce::Typeface::Ptr monoMedium_;
};

} // namespace daw::ui
