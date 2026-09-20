#include "daw/ui/panels/PlaceholderPanel.h"

namespace daw::ui
{

PlaceholderPanel::PlaceholderPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , id_(context.id)
{
    setLookAndFeel(&lookAndFeel_);
}

void PlaceholderPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto area = getLocalBounds();

    auto header = area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText(juce::String(id_).toUpperCase(), header, juce::Justification::centredLeft, false);

    // The body says the panel is not there. It does not pretend.
    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    g.drawText("panneau a venir", area, juce::Justification::centred, false);
}

} // namespace daw::ui
