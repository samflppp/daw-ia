#pragma once

#include "daw/ui/Tokens.h"
#include "daw/ui/Typography.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui
{

// The look of the application, in one place.
//
// The direction is Swiss and it is a set of refusals: no gradient, no drop
// shadow, no bevel, no rounded panel. Separation is a one-pixel rule, hierarchy
// is typographic, and colour is rare — the accent marks what the user owns
// (selection, the value being changed, an active control), white marks what is
// alive (playhead, meters), red marks recording and what is missing. Anything
// that needs three colours to be understood needs a better layout instead.
//
// Nothing here reads a literal: every colour, size and radius is a token path.
// That is not a style rule, it is what lets a second theme exist without
// touching a single component.
class DawLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit DawLookAndFeel(const Tokens& tokens);

    [[nodiscard]] const Typography& typography() const noexcept { return typography_; }

    // --- fonts
    juce::Font getLabelFont(juce::Label& label) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;

    // --- controls
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    // Faders are linear and horizontal: a track row is wide and short, and a
    // rotary knob in a list of forty tracks is a decoration nobody aims at.
    void drawLinearSlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

    void drawScrollbar(juce::Graphics& g,
                       juce::ScrollBar& scrollbar,
                       int x,
                       int y,
                       int width,
                       int height,
                       bool isScrollbarVertical,
                       int thumbStartPosition,
                       int thumbSize,
                       bool isMouseOver,
                       bool isMouseDown) override;

    int getDefaultScrollbarWidth() override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    // The focus ring is drawn, never removed: keyboard is a first-class way to
    // drive this interface, and a control nobody can see the focus of is a
    // control nobody can reach.
    void drawFocusRing(juce::Graphics& g, juce::Rectangle<float> bounds) const;

private:
    void applyColourScheme();

    const Tokens& tokens_;
    Typography typography_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DawLookAndFeel)
};

} // namespace daw::ui
