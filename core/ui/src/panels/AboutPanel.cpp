#include "daw/ui/panels/AboutPanel.h"

namespace daw::ui
{

AboutPanel::AboutPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , about_(context.about)
    , titled_(context.titled)
{
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true);

    // The mentions first, then every licence: one text, read-only, that can
    // be scrolled, selected and copied.
    licences_.setMultiLine(true, true);
    licences_.setReadOnly(true);
    licences_.setScrollbarsShown(true);
    licences_.setCaretVisible(false);
    licences_.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    licences_.setText(juce::String::fromUTF8((about_.mentions() + "\n\n" + about_.licences()).c_str()),
                      juce::dontSendNotification);
    addAndMakeVisible(licences_);
}

juce::String AboutPanel::heading() const
{
    return juce::String::fromUTF8(("DAW IA " + about_.version()).c_str());
}

juce::String AboutPanel::mentions() const
{
    return juce::String::fromUTF8(about_.mentions().c_str());
}

juce::Rectangle<int> AboutPanel::textArea() const
{
    auto area = getLocalBounds();
    if (!titled_)
        area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    return area.reduced(tokens_.integer("space.md"), tokens_.integer("space.sm"));
}

void AboutPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto area = getLocalBounds();
    if (!titled_)
    {
        auto header = area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));
        header.removeFromLeft(tokens_.integer("space.md"));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
        g.drawText(juce::String::fromUTF8("À PROPOS"), header, juce::Justification::centredLeft, false);
    }

    auto text = textArea();
    const auto title = lookAndFeel_.typography().sans("font.size.title", "font.weight.semibold");
    g.setColour(tokens_.colour("color.text.primary"));
    g.setFont(title);
    g.drawText(
        heading(), text.removeFromTop(juce::roundToInt(title.getHeight())), juce::Justification::left, true);

    const auto caption = lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular");
    g.setColour(tokens_.colour("color.text.secondary"));
    g.setFont(caption);
    g.drawText(juce::String::fromUTF8(("commit " + about_.commit() + " · prototype, octobre 2026").c_str()),
               text.removeFromTop(juce::roundToInt(caption.getHeight())),
               juce::Justification::left,
               true);
}

void AboutPanel::resized()
{
    auto text = textArea();
    const auto title = lookAndFeel_.typography().sans("font.size.title", "font.weight.semibold");
    const auto caption = lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular");
    text.removeFromTop(juce::roundToInt(title.getHeight() + caption.getHeight()) +
                       tokens_.integer("space.sm"));
    licences_.setBounds(text);
}

} // namespace daw::ui
