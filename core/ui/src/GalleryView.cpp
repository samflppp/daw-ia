#include "daw/ui/GalleryView.h"

namespace daw::ui
{

GalleryView::GalleryView(const Tokens& tokens, DawLookAndFeel& lookAndFeel)
    : tokens_(tokens)
    , lookAndFeel_(lookAndFeel)
{
    setLookAndFeel(&lookAndFeel_);

    for (auto* button : {&play_, &stop_, &record_, &disabled_})
        addAndMakeVisible(*button);

    play_.setClickingTogglesState(true);
    play_.setToggleState(true, juce::dontSendNotification);
    record_.setClickingTogglesState(true);
    record_.setColour(juce::TextButton::buttonOnColourId, tokens_.colour("color.accent.record"));
    record_.setColour(juce::TextButton::textColourOnId, tokens_.colour("color.text.primary"));
    disabled_.setEnabled(false);

    addAndMakeVisible(mute_);
    addAndMakeVisible(bypass_);
    bypass_.setToggleState(true, juce::dontSendNotification);

    for (auto* slider : {&volume_, &quiet_})
    {
        addAndMakeVisible(*slider);
        slider->setRange(-100.0, 6.0);
    }
    volume_.setValue(-3.0, juce::dontSendNotification);
    quiet_.setValue(-24.0, juce::dontSendNotification);
}

GalleryView::~GalleryView()
{
    setLookAndFeel(nullptr);
}

void GalleryView::paintSection(juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& title)
{
    auto header = area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText(title.toUpperCase(), header, juce::Justification::centredLeft, false);

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    area.removeFromTop(tokens_.integer("space.md"));
}

void GalleryView::paintSwatches(juce::Graphics& g,
                                juce::Rectangle<int>& area,
                                const std::vector<Swatch>& swatches)
{
    const auto height = tokens_.integer("metric.track.rowHeight");
    const auto gap = tokens_.integer("space.sm");

    auto row = area.removeFromTop(height);
    const auto width = (row.getWidth() - gap * static_cast<int>(swatches.size() - 1)) /
                       static_cast<int>(swatches.size());

    for (const auto& swatch : swatches)
    {
        auto cell = row.removeFromLeft(width);
        row.removeFromLeft(gap);

        auto block = cell.removeFromTop(height - tokens_.integer("space.lg"));
        g.setColour(tokens_.colour(swatch.path));
        g.fillRect(block);
        g.setColour(tokens_.colour("color.border.hairline"));
        g.drawRect(block, tokens_.integer("stroke.hairline"));

        g.setColour(tokens_.colour("color.text.tertiary"));
        g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
        g.drawText(swatch.label, cell, juce::Justification::centredLeft, false);
    }

    area.removeFromTop(tokens_.integer("space.lg"));
}

void GalleryView::paintTypeScale(juce::Graphics& g, juce::Rectangle<int>& area)
{
    struct Line
    {
        juce::String size;
        juce::String weight;
        bool mono;
        juce::String sample;
    };

    const std::vector<Line> lines{
        {"font.size.display", "font.weight.medium", true, "005.3.02"},
        {"font.size.title", "font.weight.semibold", false, "Piano-roll"},
        {"font.size.label", "font.weight.medium", false, "Bass 808"},
        {"font.size.body", "font.weight.regular", false, "Chaine de plugins de la piste"},
        {"font.size.caption", "font.weight.regular", true, "-6.2 dB"},
        {"font.size.micro", "font.weight.regular", true, "track.set_volume"},
    };

    for (const auto& line : lines)
    {
        auto row = area.removeFromTop(tokens_.integer("metric.plugin.slotHeight"));

        auto path = row.removeFromRight(row.getWidth() / 3);
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
        g.drawText(line.size, path, juce::Justification::centredRight, false);

        g.setColour(tokens_.colour("color.text.primary"));
        g.setFont(line.mono ? lookAndFeel_.typography().mono(line.size, line.weight)
                            : lookAndFeel_.typography().sans(line.size, line.weight));
        g.drawText(line.sample, row, juce::Justification::centredLeft, false);
    }

    area.removeFromTop(tokens_.integer("space.lg"));
}

void GalleryView::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.base"));

    auto area = getLocalBounds().reduced(tokens_.integer("space.xl"));

    auto left = area.removeFromLeft((area.getWidth() - tokens_.integer("space.xl")) / 2);
    area.removeFromLeft(tokens_.integer("space.xl"));
    auto right = area;

    paintSection(g, left, "Surfaces et bordures");
    paintSwatches(g,
                  left,
                  {{"color.surface.base", "base"},
                   {"color.surface.panel", "panel"},
                   {"color.surface.raised", "raised"},
                   {"color.surface.sunken", "sunken"},
                   {"color.border.hairline", "hairline"},
                   {"color.border.strong", "strong"}});

    paintSection(g, left, "Accent, vivant, acteurs");
    paintSwatches(g,
                  left,
                  {{"color.accent.primary", "primary"},
                   {"color.accent.live", "live"},
                   {"color.accent.record", "record"},
                   {"color.actor.user", "user"},
                   {"color.actor.copilot", "copilot"},
                   {"color.actor.generator", "generator"}});

    paintSection(g, left, "Grille du piano-roll");
    paintSwatches(g,
                  left,
                  {{"color.grid.bar", "bar"},
                   {"color.grid.beat", "beat"},
                   {"color.grid.subdivision", "sub"},
                   {"color.grid.rowWhite", "blanche"},
                   {"color.grid.rowBlack", "noire"},
                   {"color.note.fillSoft", "velocite"}});

    paintSection(g, right, "Echelle typographique");
    paintTypeScale(g, right);

    paintSection(g, right, "Controles");
}

void GalleryView::resized()
{
    auto area = getLocalBounds().reduced(tokens_.integer("space.xl"));

    area.removeFromLeft((area.getWidth() - tokens_.integer("space.xl")) / 2);
    area.removeFromLeft(tokens_.integer("space.xl"));

    // The gallery mirrors what paint() lays out above it: three sections on the
    // left, the type scale and the controls on the right.
    const auto sectionHeight = tokens_.integer("metric.panel.headerHeight") + tokens_.integer("space.md");
    area.removeFromTop(sectionHeight);
    area.removeFromTop((tokens_.integer("metric.plugin.slotHeight") * 6) + tokens_.integer("space.lg"));
    area.removeFromTop(sectionHeight);

    auto buttons = area.removeFromTop(tokens_.integer("metric.transport.buttonSize"));
    const auto gap = tokens_.integer("space.sm");
    const auto buttonWidth = tokens_.integer("metric.transport.buttonSize") * 3;

    for (auto* button : {&play_, &stop_, &record_, &disabled_})
    {
        button->setBounds(buttons.removeFromLeft(buttonWidth));
        buttons.removeFromLeft(gap);
    }

    area.removeFromTop(tokens_.integer("space.lg"));

    auto switches = area.removeFromTop(tokens_.integer("metric.track.rowHeight"));
    mute_.setBounds(switches.removeFromLeft(tokens_.integer("metric.track.chipWidth")));
    switches.removeFromLeft(gap);
    bypass_.setBounds(switches.removeFromLeft(tokens_.integer("metric.track.chipWidth")));

    area.removeFromTop(tokens_.integer("space.lg"));

    volume_.setBounds(area.removeFromTop(tokens_.integer("metric.track.rowHeight")));
    quiet_.setBounds(area.removeFromTop(tokens_.integer("metric.track.rowHeight")));
}

} // namespace daw::ui
