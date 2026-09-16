#pragma once

#include <juce_graphics/juce_graphics.h>

namespace daw::ui
{

// Read-only access to design tokens (core/ui/tokens/tokens.json).
//
// Hygiene rule 1: every visual value (colour, spacing, size, radius, font size)
// comes from here. Paths use dots: "color.surface.base", "space.md".
class Tokens
{
public:
    // Tokens embedded in the binary at build time.
    [[nodiscard]] static const Tokens& builtIn();

    [[nodiscard]] static Tokens fromJson(const juce::String& json);

    // Accepts "#RRGGBB" and "#RRGGBBAA".
    [[nodiscard]] juce::Colour colour(juce::StringRef path) const;
    [[nodiscard]] float number(juce::StringRef path) const;
    [[nodiscard]] int integer(juce::StringRef path) const;

    [[nodiscard]] bool contains(juce::StringRef path) const;

private:
    explicit Tokens(juce::var root);

    [[nodiscard]] juce::var lookup(juce::StringRef path) const;

    juce::var root_;
};

} // namespace daw::ui
