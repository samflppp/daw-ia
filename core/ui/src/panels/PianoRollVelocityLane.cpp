// The piano roll's velocity lane: FL's event editor, under the notes.
//
// One stem per note, at its start, as tall as its velocity. The hand draws
// over the stems:
//   click                  the notes under the pointer take the height
//                          clicked — every note of a chord at once
//   drag                   a line: each stem the pointer crosses takes the
//                          height of the line where it crosses, so a
//                          crescendo is one stroke
// With two notes or more picked in the grid, only those are touched, as in FL.
// The stroke is shown as it is drawn and sent when the mouse is released, as
// one group: one history entry however many notes it changed. Sent note by
// note, the history would keep one entry per note, since two notes changed in
// one gesture stay two entries.
//
// Alt + drag on a note, in the grid, still changes that note alone.

#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/ui/panels/PianoRollPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace daw::ui
{

juce::Rectangle<int> PianoRollPanel::velocityArea() const
{
    auto area = bodyArea();
    area.removeFromLeft(tokens_.integer("metric.pianoRoll.keyboardWidth"));
    return area.removeFromBottom(tokens_.integer("metric.pianoRoll.velocityLaneHeight"));
}

int PianoRollPanel::yForVelocity(int velocity) const
{
    const auto handle = tokens_.integer("metric.pianoRoll.velocityHandle");
    const auto lane = velocityArea().withTrimmedTop(handle * 2).withTrimmedBottom(handle);
    const auto amount = static_cast<double>(velocity - domain::Note::lowestVelocity) /
                        static_cast<double>(domain::Note::highestVelocity - domain::Note::lowestVelocity);
    return lane.getBottom() - static_cast<int>(std::lround(amount * lane.getHeight()));
}

int PianoRollPanel::velocityAtY(int y) const
{
    const auto handle = tokens_.integer("metric.pianoRoll.velocityHandle");
    const auto lane = velocityArea().withTrimmedTop(handle * 2).withTrimmedBottom(handle);
    const auto amount = static_cast<double>(lane.getBottom() - y) / std::max(1, lane.getHeight());
    const auto range = domain::Note::highestVelocity - domain::Note::lowestVelocity;
    return std::clamp(domain::Note::lowestVelocity + static_cast<int>(std::lround(amount * range)),
                      domain::Note::lowestVelocity,
                      domain::Note::highestVelocity);
}

juce::Point<int> PianoRollPanel::velocityPointFor(const domain::Note& note, int velocity) const
{
    return {xForBeat(note.startBeats), yForVelocity(velocity)};
}

bool PianoRollPanel::isVelocityEditable(domain::NoteId id) const
{
    return picked_.size() < 2 || isPicked(id);
}

int PianoRollPanel::shownVelocity(const domain::Note& note) const
{
    if (velocityStroke_.has_value())
    {
        for (const auto& [id, velocity] : *velocityStroke_)
        {
            if (id == note.id)
                return velocity;
        }
    }
    return note.velocity;
}

void PianoRollPanel::paintVelocityLane(juce::Graphics& g) const
{
    const auto lane = velocityArea();
    const auto hairline = tokens_.integer("stroke.hairline");
    const auto label = juce::Rectangle<int>{
        0, lane.getY(), tokens_.integer("metric.pianoRoll.keyboardWidth"), lane.getHeight()};

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(label);
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText(juce::String{u8"VÉLOCITÉ"},
               label.reduced(tokens_.integer("space.xs"), 0),
               juce::Justification::centredLeft,
               false);

    g.setColour(tokens_.colour("color.surface.sunken"));
    g.fillRect(lane);

    // A guide at 100, the velocity a drawn note gets: what "louder than
    // usual" and "softer than usual" are read against.
    g.setColour(tokens_.colour("color.grid.beat"));
    g.fillRect(lane.getX(), yForVelocity(100), lane.getWidth(), hairline);

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(0, lane.getY(), getWidth(), hairline);

    const auto* edited = clip();
    if (edited == nullptr)
        return;

    g.saveState();
    g.reduceClipRegion(lane);

    const auto stem = tokens_.integer("metric.pianoRoll.velocityStem");
    const auto radius = tokens_.number("metric.pianoRoll.velocityHandle");

    // The ones the hand cannot reach first, so a reachable stem is never
    // drawn under one it cannot change.
    for (const auto editable : {false, true})
    {
        for (const auto& note : edited->notes)
        {
            if (isVelocityEditable(note.id) != editable)
                continue;

            const auto top = velocityPointFor(note, shownVelocity(note));
            const auto colour = !editable ? tokens_.colour("color.note.fillSoft")
                                          : (isPicked(note.id) || note.id == selectedNote_
                                                 ? tokens_.colour("color.note.selected")
                                                 : tokens_.colour("color.note.fill"));
            g.setColour(colour);
            g.fillRect(top.getX(), top.getY(), stem, lane.getBottom() - top.getY());
            g.fillEllipse(juce::Rectangle<float>{radius * 2.0f, radius * 2.0f}.withCentre(
                top.toFloat().translated(static_cast<float>(stem) / 2.0f, 0.0f)));
        }
    }

    g.restoreState();
}

void PianoRollPanel::strokeVelocity(juce::Point<int> from, juce::Point<int> to)
{
    const auto* edited = clip();
    if (edited == nullptr || !velocityStroke_.has_value())
        return;

    // A stem is crossed when the pointer passes over it, give or take the
    // width of its head: a click next to a stem still reaches it.
    const auto reach = tokens_.integer("metric.pianoRoll.velocityHandle") * 2;
    // Zoomed in, a stem out of the window cannot be crossed.
    const auto grid = gridArea();
    const auto left = std::max(std::min(from.getX(), to.getX()) - reach, grid.getX());
    const auto right = std::min(std::max(from.getX(), to.getX()) + reach, grid.getRight());

    for (const auto& note : edited->notes)
    {
        if (!isVelocityEditable(note.id))
            continue;

        const auto x = xForBeat(note.startBeats);
        if (x < left || x > right)
            continue;

        // Where the line drawn by the hand is, above this stem.
        const auto span = to.getX() - from.getX();
        const auto along =
            span == 0 ? 1.0 : std::clamp(static_cast<double>(x - from.getX()) / span, 0.0, 1.0);
        const auto y = static_cast<int>(std::lround(from.getY() + along * (to.getY() - from.getY())));
        const auto velocity = velocityAtY(y);

        auto& stroke = *velocityStroke_;
        const auto found = std::find_if(
            stroke.begin(), stroke.end(), [&note](const auto& entry) { return entry.first == note.id; });
        if (found == stroke.end())
        {
            stroke.emplace_back(note.id, velocity);
            continue;
        }
        // The reach is for a stem not yet touched. One the stroke has set
        // changes again only when the hand passes over it once more: else
        // the next segment, a few pixels on, gave the first stem of a
        // crescendo the pointer's later height (63 for 60, S22).
        if (x >= std::min(from.getX(), to.getX()) && x <= std::max(from.getX(), to.getX()))
            found->second = velocity;
    }
    repaint(velocityArea());
}

void PianoRollPanel::commitVelocityStroke()
{
    if (!velocityStroke_.has_value())
        return;

    const auto stroke = std::move(*velocityStroke_);
    velocityStroke_.reset();

    const auto* edited = clip();
    if (edited == nullptr)
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    for (const auto& [id, velocity] : stroke)
    {
        const auto found = std::find_if(edited->notes.begin(),
                                        edited->notes.end(),
                                        [id](const domain::Note& note) { return note.id == id; });
        if (found != edited->notes.end() && found->velocity != velocity)
            commands.push_back(std::make_unique<domain::SetNoteVelocity>(edited->id, id, velocity));
    }

    if (!commands.empty())
    {
        domain::GroupOptions group;
        group.label = commands.size() > 1 ? "vélocités" : "vélocité";
        static_cast<void>(bus_.executeGroup(std::move(commands), group));
    }
    repaint();
}

} // namespace daw::ui
