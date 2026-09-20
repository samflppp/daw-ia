#include "daw/ui/DawLookAndFeel.h"

namespace daw::ui
{

DawLookAndFeel::DawLookAndFeel(const Tokens& tokens)
    : tokens_(tokens)
    , typography_(tokens)
{
    applyColourScheme();
}

void DawLookAndFeel::applyColourScheme()
{
    const auto base = tokens_.colour("color.surface.base");
    const auto panel = tokens_.colour("color.surface.panel");
    const auto raised = tokens_.colour("color.surface.raised");
    const auto text = tokens_.colour("color.text.primary");
    const auto secondary = tokens_.colour("color.text.secondary");
    const auto disabled = tokens_.colour("color.text.disabled");
    const auto hairline = tokens_.colour("color.border.hairline");
    const auto accent = tokens_.colour("color.accent.primary");

    setColour(juce::ResizableWindow::backgroundColourId, base);
    setColour(juce::DocumentWindow::backgroundColourId, base);

    setColour(juce::Label::textColourId, text);
    setColour(juce::Label::backgroundColourId, juce::Colour{});
    setColour(juce::Label::outlineColourId, juce::Colour{});

    setColour(juce::TextButton::buttonColourId, panel);
    setColour(juce::TextButton::buttonOnColourId, accent);
    setColour(juce::TextButton::textColourOffId, secondary);
    setColour(juce::TextButton::textColourOnId, tokens_.colour("color.accent.onPrimary"));

    setColour(juce::ToggleButton::textColourId, secondary);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::ToggleButton::tickDisabledColourId, disabled);

    setColour(juce::Slider::backgroundColourId, tokens_.colour("color.surface.sunken"));
    setColour(juce::Slider::trackColourId, secondary);
    setColour(juce::Slider::thumbColourId, accent);

    setColour(juce::ScrollBar::backgroundColourId, panel);
    setColour(juce::ScrollBar::thumbColourId, tokens_.colour("color.border.strong"));

    setColour(juce::PopupMenu::backgroundColourId, raised);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, tokens_.colour("color.state.selected"));
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::PopupMenu::headerTextColourId, tokens_.colour("color.text.tertiary"));

    setColour(juce::TextEditor::backgroundColourId, tokens_.colour("color.surface.sunken"));
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, hairline);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::TextEditor::highlightColourId, tokens_.colour("color.state.selected"));

    setColour(juce::TooltipWindow::backgroundColourId, raised);
    setColour(juce::TooltipWindow::textColourId, text);
    setColour(juce::TooltipWindow::outlineColourId, hairline);
}

juce::Font DawLookAndFeel::getLabelFont(juce::Label& label)
{
    juce::ignoreUnused(label);
    return typography_.sans("font.size.body", "font.weight.regular");
}

juce::Font DawLookAndFeel::getTextButtonFont(juce::TextButton& button, int buttonHeight)
{
    juce::ignoreUnused(button, buttonHeight);
    return typography_.sans("font.size.caption", "font.weight.medium");
}

juce::Font DawLookAndFeel::getPopupMenuFont()
{
    return typography_.sans("font.size.body", "font.weight.regular");
}

void DawLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                          juce::Button& button,
                                          const juce::Colour& backgroundColour,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(tokens_.number("stroke.hairline") * 0.5f);
    const auto radius = tokens_.number("radius.sm");
    const bool on = button.getToggleState();

    // An active control is filled with the accent; an idle one is a rule and
    // nothing else. There is no third state made of a darker grey: a button
    // that is neither on nor off is a button the user has to decode.
    if (on)
    {
        g.setColour(backgroundColour);
        g.fillRoundedRectangle(bounds, radius);
        return;
    }

    if (shouldDrawButtonAsDown)
        g.setColour(tokens_.colour("color.state.pressed"));
    else if (shouldDrawButtonAsHighlighted)
        g.setColour(tokens_.colour("color.state.hover"));
    else
        g.setColour(juce::Colour{});

    g.fillRoundedRectangle(bounds, radius);

    g.setColour(tokens_.colour("color.border.hairline"));
    g.drawRoundedRectangle(bounds, radius, tokens_.number("stroke.hairline"));

    if (button.hasKeyboardFocus(false))
        drawFocusRing(g, bounds);
}

void DawLookAndFeel::drawButtonText(juce::Graphics& g,
                                    juce::TextButton& button,
                                    bool shouldDrawButtonAsHighlighted,
                                    bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsDown);

    const auto colourId =
        button.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId;
    auto colour = button.findColour(colourId);

    if (!button.isEnabled())
        colour = tokens_.colour("color.text.disabled");
    else if (shouldDrawButtonAsHighlighted && !button.getToggleState())
        colour = tokens_.colour("color.text.primary");

    g.setColour(colour);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawText(button.getButtonText(),
               button.getLocalBounds().reduced(tokens_.integer("space.sm"), 0),
               juce::Justification::centred,
               false);
}

void DawLookAndFeel::drawToggleButton(juce::Graphics& g,
                                      juce::ToggleButton& button,
                                      bool shouldDrawButtonAsHighlighted,
                                      bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused(shouldDrawButtonAsDown);

    const auto box =
        button.getLocalBounds()
            .toFloat()
            .withWidth(tokens_.number("metric.track.chipWidth"))
            .withHeight(tokens_.number("metric.track.chipHeight"))
            .withY((static_cast<float>(button.getHeight()) - tokens_.number("metric.track.chipHeight")) *
                   0.5f)
            .reduced(tokens_.number("stroke.hairline") * 0.5f);

    const auto radius = tokens_.number("radius.sm");

    // A switch reads as filled or not filled. No tick, no cross: at eleven
    // pixels a glyph is noise, and the state has to be legible out of the
    // corner of the eye while the user is listening rather than looking.
    if (button.getToggleState())
    {
        g.setColour(tokens_.colour("color.accent.primary"));
        g.fillRoundedRectangle(box, radius);
    }
    else
    {
        if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(tokens_.colour("color.state.hover"));
            g.fillRoundedRectangle(box, radius);
        }

        g.setColour(tokens_.colour("color.border.hairline"));
        g.drawRoundedRectangle(box, radius, tokens_.number("stroke.hairline"));
    }

    g.setColour(button.getToggleState() ? tokens_.colour("color.accent.onPrimary")
                                        : tokens_.colour("color.text.disabled"));
    g.setFont(typography_.sans("font.size.micro", "font.weight.semibold"));
    g.drawText(button.getButtonText(), box, juce::Justification::centred, false);

    if (button.hasKeyboardFocus(false))
        drawFocusRing(g, box);
}

void DawLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                      int x,
                                      int y,
                                      int width,
                                      int height,
                                      float sliderPos,
                                      float minSliderPos,
                                      float maxSliderPos,
                                      juce::Slider::SliderStyle style,
                                      juce::Slider& slider)
{
    juce::ignoreUnused(minSliderPos, maxSliderPos, style);

    const auto thickness = tokens_.number("metric.track.faderHeight");
    const auto radius = tokens_.number("radius.sm");

    const juce::Rectangle<float> groove{static_cast<float>(x),
                                        static_cast<float>(y) +
                                            (static_cast<float>(height) - thickness) * 0.5f,
                                        static_cast<float>(width),
                                        thickness};

    g.setColour(slider.findColour(juce::Slider::backgroundColourId));
    g.fillRoundedRectangle(groove, radius);

    // The filled part is the value. There is no thumb to draw: a bar this thin
    // reads as a level, and a knob on top of it would only hide the value it
    // is meant to show.
    auto filled = groove.withRight(sliderPos);

    const bool active = slider.isMouseOverOrDragging() || slider.hasKeyboardFocus(false);
    g.setColour(active ? tokens_.colour("color.accent.primary")
                       : slider.findColour(juce::Slider::trackColourId));
    g.fillRoundedRectangle(filled, radius);

    if (slider.hasKeyboardFocus(false))
        drawFocusRing(g, groove.expanded(tokens_.number("space.xxs")));
}

void DawLookAndFeel::drawScrollbar(juce::Graphics& g,
                                   juce::ScrollBar& scrollbar,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   bool isScrollbarVertical,
                                   int thumbStartPosition,
                                   int thumbSize,
                                   bool isMouseOver,
                                   bool isMouseDown)
{
    juce::ignoreUnused(scrollbar);

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(x, y, width, height);

    if (thumbSize <= 0)
        return;

    const auto inset = tokens_.integer("space.xs");
    const auto thumb =
        isScrollbarVertical
            ? juce::Rectangle<int>{x + inset, thumbStartPosition, width - inset * 2, thumbSize}
            : juce::Rectangle<int>{thumbStartPosition, y + inset, thumbSize, height - inset * 2};

    if (isMouseDown)
        g.setColour(tokens_.colour("color.text.tertiary"));
    else if (isMouseOver)
        g.setColour(tokens_.colour("color.border.strong").brighter());
    else
        g.setColour(tokens_.colour("color.border.strong"));

    g.fillRoundedRectangle(thumb.toFloat(), tokens_.number("radius.sm"));
}

int DawLookAndFeel::getDefaultScrollbarWidth()
{
    return tokens_.integer("metric.scrollbar.thickness");
}

void DawLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(tokens_.colour("color.surface.raised"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.drawRect(0, 0, width, height, tokens_.integer("stroke.hairline"));
}

void DawLookAndFeel::drawFocusRing(juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    g.setColour(tokens_.colour("color.state.focus"));
    g.drawRoundedRectangle(bounds, tokens_.number("radius.sm"), tokens_.number("stroke.focus"));
}

} // namespace daw::ui
