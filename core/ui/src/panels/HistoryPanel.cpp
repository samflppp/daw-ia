#include "daw/ui/panels/HistoryPanel.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

// The colour of the actor, so that a copilot's edit is told apart from the
// user's without reading a word.
[[nodiscard]] const char* actorColourPath(domain::Actor actor) noexcept
{
    switch (actor)
    {
    case domain::Actor::copilot:
        return "color.actor.copilot";
    case domain::Actor::generator:
        return "color.actor.generator";
    case domain::Actor::user:
    default:
        return "color.actor.user";
    }
}

[[nodiscard]] const char* actorLabel(domain::Actor actor) noexcept
{
    switch (actor)
    {
    case domain::Actor::copilot:
        return "copilote";
    case domain::Actor::generator:
        return "generateur";
    case domain::Actor::user:
    default:
        return "vous";
    }
}

} // namespace

HistoryPanel::HistoryPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , history_(context.history)
{
    setLookAndFeel(&lookAndFeel_);

    addAndMakeVisible(undo_);
    addAndMakeVisible(redo_);

    // The two moves the bus offers, said in words. The transport has the same
    // two as icons; a beginner reads these, and both send the same call.
    undo_.onClick = [this] { static_cast<void>(bus_.undo()); };
    redo_.onClick = [this] { static_cast<void>(bus_.redo()); };

    history_.addChangeListener(this);
    refresh();
}

HistoryPanel::~HistoryPanel()
{
    history_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void HistoryPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    refresh();
}

void HistoryPanel::refresh()
{
    undo_.setEnabled(bus_.canUndo());
    redo_.setEnabled(bus_.canRedo());
    repaint();
}

juce::Rectangle<int> HistoryPanel::listArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromBottom(tokens_.integer("metric.plugin.slotHeight"));
    return area;
}

int HistoryPanel::rowAt(juce::Point<int> point) const
{
    const auto area = listArea();
    if (!area.contains(point))
        return -1;

    const auto row = (point.getY() - area.getY()) / tokens_.integer("metric.history.rowHeight");
    return row < static_cast<int>(history_.entries().size()) ? row : -1;
}

std::size_t HistoryPanel::entryAtRow(int row) const
{
    // Newest first: row 0 is the last entry.
    return history_.entries().size() - 1 - static_cast<std::size_t>(row);
}

void HistoryPanel::walkTo(std::size_t entryIndex)
{
    // The target is the cursor the click asks for: everything up to and
    // including the entry stays applied, the rest is undone.
    const auto wanted = entryIndex + 1;

    while (history_.cursor() > wanted && bus_.canUndo())
    {
        if (!bus_.undo().ok())
            break;
    }

    while (history_.cursor() < wanted && bus_.canRedo())
    {
        if (!bus_.redo().ok())
            break;
    }
}

void HistoryPanel::mouseDown(const juce::MouseEvent& event)
{
    const auto row = rowAt(event.getPosition());
    if (row < 0)
        return;

    walkTo(entryAtRow(row));
}

void HistoryPanel::mouseMove(const juce::MouseEvent& event)
{
    const auto row = rowAt(event.getPosition());
    if (row == hovered_)
        return;

    hovered_ = row;
    repaint();
}

void HistoryPanel::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    hovered_ = -1;
    repaint();
}

void HistoryPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    auto count = header.removeFromRight(tokens_.integer("space.xl"));

    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText("HISTORIQUE", header, juce::Justification::centredLeft, false);

    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
    g.drawText(
        juce::String(static_cast<int>(history_.cursor())), count, juce::Justification::centredLeft, false);

    const auto& entries = history_.entries();
    if (entries.empty())
    {
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
        g.drawText("Rien encore", listArea(), juce::Justification::centred, false);
        return;
    }

    auto area = listArea();
    const auto rowHeight = tokens_.integer("metric.history.rowHeight");
    const auto rows = std::min(static_cast<int>(entries.size()), area.getHeight() / rowHeight);

    for (int row = 0; row < rows; ++row)
    {
        const auto index = entryAtRow(row);
        const auto& entry = entries[index];

        // Past the cursor means undone: still there, still redoable, and shown
        // as what it is rather than removed from the list.
        const bool applied = index < history_.cursor();
        auto line = area.removeFromTop(rowHeight);

        if (row == hovered_)
        {
            g.setColour(tokens_.colour("color.state.hover"));
            g.fillRect(line.withTrimmedLeft(tokens_.integer("space.xs")));
        }

        if (index + 1 == history_.cursor())
        {
            g.setColour(tokens_.colour("color.accent.primary"));
            g.fillRect(line.removeFromLeft(tokens_.integer("stroke.focus")));
        }

        line.removeFromLeft(tokens_.integer("space.md"));

        // The dot is the author. It is drawn full for an applied entry and
        // outlined for an undone one.
        const auto dotSize = tokens_.integer("metric.history.dotSize");
        auto dot = line.removeFromLeft(tokens_.integer("space.md"))
                       .withSizeKeepingCentre(dotSize, dotSize)
                       .toFloat();

        g.setColour(tokens_.colour(actorColourPath(entry.actor)));
        if (applied)
            g.fillEllipse(dot);
        else
            g.drawEllipse(dot, static_cast<float>(tokens_.integer("stroke.hairline")));

        line.removeFromLeft(tokens_.integer("space.sm"));

        // The same margin as the left inset, or the count of a coalesced
        // gesture is drawn half outside the panel.
        line.removeFromRight(tokens_.integer("space.md"));

        auto merged = line.removeFromRight(tokens_.integer("space.xl"));
        if (entry.merged > 1)
        {
            g.setColour(tokens_.colour("color.text.disabled"));
            g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
            g.drawText("x" + juce::String(static_cast<int>(entry.merged)),
                       merged,
                       juce::Justification::centredRight,
                       false);
        }

        auto author = line.removeFromRight(tokens_.integer("space.xl") * 2);
        g.setColour(tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
        g.drawText(actorLabel(entry.actor), author, juce::Justification::centredRight, false);

        const auto label = History::describe(entry.type);
        g.setColour(applied ? tokens_.colour("color.text.secondary") : tokens_.colour("color.text.disabled"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
        g.drawText(juce::String(label.data(), label.size()), line, juce::Justification::centredLeft, true);
    }
}

void HistoryPanel::resized()
{
    auto footer = getLocalBounds().removeFromBottom(tokens_.integer("metric.plugin.slotHeight"));
    undo_.setBounds(footer.removeFromLeft(footer.getWidth() / 2));
    redo_.setBounds(footer);
}

} // namespace daw::ui
