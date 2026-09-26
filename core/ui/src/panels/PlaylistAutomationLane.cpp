// The playlist's automation lanes: one per line of the project, under the
// audio lane of its track when it has one, strip by strip otherwise.
//
// The grammar is the tempo lane's, so nothing is learnt twice:
//
//   click on the lane          a point at the beat under the pointer (Shift:
//                              wherever the pointer is), at the value under
//                              it; then dragged like any other
//   drag a point               moves it in time and in value at once, one
//                              history entry
//   right-click a point        removes it
//   double-click a point       removes it too
//   wheel over a point         its value, one step a notch (a dB, a
//                              twentieth of the pan, a hundredth of a
//                              parameter); with Alt, the curve that leaves it
//   right-click the name       "Supprimer la ligne"
//
// The line is drawn as the engine plays it: sampled through the domain's
// valueAt, the volume in fader position. A line with no point is drawn dashed
// at the target's static value, the value that plays while it is empty.

#include "daw/domain/commands/AutomationCommands.h"
#include "daw/ui/model/AutomationEditing.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace daw::ui
{
namespace
{

// How long the wheel rests before its turn is over, as on the tempo lane.
constexpr juce::uint32 wheelRestMs = 500;

} // namespace

juce::Rectangle<int> PlaylistPanel::laneArea(int lane) const
{
    const auto grid = gridArea();
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    return {grid.getX(), grid.getY() + lane * laneHeight - firstLanePixel(), grid.getWidth(), laneHeight};
}

const domain::AutomationLine* PlaylistPanel::automationLineIn(int lane) const
{
    const auto all = lanes();
    if (lane < 0 || lane >= static_cast<int>(all.size()))
        return nullptr;
    const auto& entry = all[static_cast<std::size_t>(lane)];
    return entry.kind == Lane::Kind::automation ? state_.findAutomationLine(entry.line) : nullptr;
}

int PlaylistPanel::yForValue(juce::Rectangle<int> area,
                             const domain::AutomationTarget& target,
                             double value) const
{
    const auto handle = tokens_.integer("metric.playlist.tempoHandle");
    const auto inner = area.withTrimmedTop(handle).withTrimmedBottom(handle);
    const auto height = automationEditing::heightOf(target, value);
    return inner.getBottom() - static_cast<int>(std::lround(height * inner.getHeight()));
}

double PlaylistPanel::valueAtY(juce::Rectangle<int> area, const domain::AutomationTarget& target, int y) const
{
    const auto handle = tokens_.integer("metric.playlist.tempoHandle");
    const auto inner = area.withTrimmedTop(handle).withTrimmedBottom(handle);
    const auto height = static_cast<double>(inner.getBottom() - y) / std::max(1, inner.getHeight());
    return automationEditing::valueAtHeight(target, height);
}

std::optional<int> PlaylistPanel::laneOfAutomation(domain::AutomationLineId line) const
{
    const auto all = lanes();
    for (std::size_t index = 0; index < all.size(); ++index)
    {
        if (all[index].kind == Lane::Kind::automation && all[index].line == line)
            return static_cast<int>(index);
    }
    return {};
}

std::optional<juce::Point<int>>
PlaylistPanel::automationPointFor(domain::AutomationLineId line, double beats, double value) const
{
    const auto lane = laneOfAutomation(line);
    const auto* source = state_.findAutomationLine(line);
    if (!lane.has_value() || source == nullptr)
        return {};
    return juce::Point<int>{xForBeat(beats), yForValue(laneArea(*lane), source->target, value)};
}

std::optional<domain::AutomationPointId> PlaylistPanel::automationPointAt(int lane,
                                                                          juce::Point<int> point) const
{
    const auto* line = automationLineIn(lane);
    if (line == nullptr)
        return {};

    // The handle, and a little around it: a point is small, a hand is not.
    const auto reach = tokens_.integer("metric.playlist.tempoHandle") * 2;
    const auto area = laneArea(lane);

    std::optional<domain::AutomationPointId> nearest;
    auto best = reach + 1;
    for (const auto& candidate : line->points)
    {
        const auto handle =
            juce::Point<int>{xForBeat(candidate.beats), yForValue(area, line->target, candidate.value)};
        const auto distance = static_cast<int>(std::lround(handle.getDistanceFrom(point)));
        if (distance <= reach && distance < best)
        {
            best = distance;
            nearest = candidate.id;
        }
    }
    return nearest;
}

void PlaylistPanel::paintAutomation(juce::Graphics& g, juce::Rectangle<int> grid) const
{
    const auto all = lanes();
    const auto radius = tokens_.number("metric.playlist.tempoHandle");

    g.saveState();
    g.reduceClipRegion(grid);

    for (int lane = 0; lane < static_cast<int>(all.size()); ++lane)
    {
        const auto* line = automationLineIn(lane);
        if (line == nullptr)
            continue;
        const auto area = laneArea(lane);
        if (!area.intersects(grid))
            continue;

        // Nothing drives the target yet: the value it plays, dashed.
        if (line->points.empty())
        {
            const auto y = static_cast<float>(
                yForValue(area, line->target, automationEditing::staticValue(state_, line->target)));
            const float dashes[] = {4.0f, 4.0f};
            g.setColour(tokens_.colour("color.text.tertiary"));
            g.drawDashedLine(
                juce::Line<float>{static_cast<float>(area.getX()), y, static_cast<float>(area.getRight()), y},
                dashes,
                2,
                tokens_.number("stroke.hairline"));
            continue;
        }

        // Sampled every few pixels through the domain, which bends the way
        // the engine does: the line drawn is the line heard.
        juce::Path path;
        juce::Path shade;
        const auto step = std::max(1, tokens_.integer("space.xs"));
        const auto bottom = static_cast<float>(area.getBottom());
        shade.startNewSubPath(static_cast<float>(area.getX()), bottom);
        for (int x = area.getX(); x <= area.getRight(); x += step)
        {
            const auto y = static_cast<float>(yForValue(area, line->target, line->valueAt(beatAtX(x))));
            if (x == area.getX())
                path.startNewSubPath(static_cast<float>(x), y);
            else
                path.lineTo(static_cast<float>(x), y);
            shade.lineTo(static_cast<float>(x), y);
        }
        shade.lineTo(static_cast<float>(area.getRight()), bottom);
        shade.closeSubPath();

        g.setColour(tokens_.colour("color.state.selected"));
        g.fillPath(shade);
        g.setColour(tokens_.colour("color.accent.primary"));
        g.strokePath(path, juce::PathStrokeType{tokens_.number("stroke.focus")});

        for (const auto& point : line->points)
        {
            const auto handle =
                juce::Point<float>{static_cast<float>(xForBeat(point.beats)),
                                   static_cast<float>(yForValue(area, line->target, point.value))};
            g.fillEllipse(juce::Rectangle<float>{radius * 2.0f, radius * 2.0f}.withCentre(handle));
        }
    }

    g.restoreState();
}

void PlaylistPanel::revealAutomation(domain::AutomationLineId line)
{
    if (line.isNil())
        return;

    shownAutomation_ = line;
    if (const auto lane = laneOfAutomation(line); lane.has_value())
    {
        const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
        const auto top = *lane * laneHeight;
        const auto visible = gridArea().getHeight();
        if (top < firstLanePixel() || top + laneHeight > firstLanePixel() + visible)
            setFirstLanePixel(top - std::max(0, visible - laneHeight) / 2);
    }
    repaint();
}

void PlaylistPanel::showAutomationMenu(domain::AutomationLineId line)
{
    juce::PopupMenu menu;
    menu.addItem(1, juce::String::fromUTF8(u8"Supprimer la ligne"));

    juce::Component::SafePointer<PlaylistPanel> safe{this};
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                       [safe, line](int chosen)
                       {
                           if (safe == nullptr || chosen != 1)
                               return;
                           static_cast<void>(
                               safe->bus_.execute(std::make_unique<domain::RemoveAutomationLine>(line)));
                       });
}

bool PlaylistPanel::automationMouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    const auto lane = laneAtY(point.getY());
    const auto* line = automationLineIn(lane);
    if (line == nullptr)
        return false;

    closeAutomationWheel();
    const auto lineId = line->id;

    if (headerArea().contains(point))
    {
        if (event.mods.isRightButtonDown())
            showAutomationMenu(lineId);
        else
            revealAutomation(lineId);
        return true;
    }

    if (!gridArea().contains(point))
        return true;

    auto hit = automationPointAt(lane, point);

    if (event.mods.isRightButtonDown())
    {
        if (hit.has_value())
            static_cast<void>(bus_.execute(std::make_unique<domain::RemoveAutomationPoint>(lineId, *hit)));
        return true;
    }

    if (!hit.has_value())
    {
        // A new point where the hand is, then dragged like any other.
        const auto raw = std::max(0.0, beatAtX(point.getX()));
        const auto beats = event.mods.isShiftDown() ? raw : snap(raw, true);
        domain::AutomationPoint made{};
        made.id = domain::AutomationPointId::generate();
        made.beats = beats;
        made.value = valueAtY(laneArea(lane), line->target, point.getY());
        if (!bus_.execute(std::make_unique<domain::AddAutomationPoint>(lineId, made)).ok())
            return true;
        hit = made.id;
    }

    AutomationDrag drag;
    drag.line = lineId;
    drag.point = *hit;
    drag.gesture = bus_.beginGesture("point d'automation");
    automationDrag_ = drag;
    return true;
}

bool PlaylistPanel::automationMouseDrag(const juce::MouseEvent& event)
{
    if (!automationDrag_.has_value())
        return false;

    const auto& drag = *automationDrag_;
    const auto* line = state_.findAutomationLine(drag.line);
    const auto* current = line != nullptr ? line->findPoint(drag.point) : nullptr;
    const auto lane = laneOfAutomation(drag.line);
    if (current == nullptr || !lane.has_value())
        return true;

    const auto point = event.getPosition();
    const auto raw = std::max(0.0, beatAtX(point.getX()));
    const auto beats = event.mods.isShiftDown() ? raw : snap(raw, true);
    const auto value = valueAtY(laneArea(*lane), line->target, point.getY());
    if (beats == current->beats && value == current->value)
        return true;

    // Onto another point, the command refuses and the point stays where it
    // last could be.
    domain::ExecuteOptions options;
    options.gesture = drag.gesture;
    static_cast<void>(bus_.execute(
        std::make_unique<domain::MoveAutomationPoint>(drag.line, drag.point, beats, value), options));
    return true;
}

bool PlaylistPanel::automationMouseUp()
{
    if (!automationDrag_.has_value())
        return false;

    if (bus_.openGesture() == automationDrag_->gesture)
        static_cast<void>(bus_.endGesture(automationDrag_->gesture));
    automationDrag_.reset();
    return true;
}

bool PlaylistPanel::automationDoubleClick(juce::Point<int> point)
{
    const auto lane = laneAtY(point.getY());
    const auto* line = automationLineIn(lane);
    if (line == nullptr || !gridArea().contains(point))
        return false;

    // The click that came before the double-click put a point there, or
    // grabbed the one there: either way it is the one under the pointer.
    if (const auto hit = automationPointAt(lane, point); hit.has_value())
        static_cast<void>(bus_.execute(std::make_unique<domain::RemoveAutomationPoint>(line->id, *hit)));
    return true;
}

bool PlaylistPanel::automationWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto point = event.getPosition();
    const auto lane = laneAtY(point.getY());
    const auto* line = automationLineIn(lane);
    if (line == nullptr || !gridArea().contains(point) || event.mods.isCtrlDown() ||
        event.mods.isShiftDown() || wheel.deltaX != 0.0f)
        return false;

    const auto hit = automationPointAt(lane, point);
    if (!hit.has_value())
        return false;

    const auto notches = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    const auto* current = line->findPoint(*hit);
    if (notches == 0 || current == nullptr)
        return true;

    lastAutomationWheelMs_ = juce::Time::getMillisecondCounter();
    if (!automationWheelGesture_.has_value() || bus_.openGesture() != automationWheelGesture_)
        automationWheelGesture_ = bus_.beginGesture("molette sur l'automation");

    domain::ExecuteOptions options;
    options.gesture = automationWheelGesture_;

    if (event.mods.isAltDown())
    {
        const auto curve = automationEditing::stepCurve(current->curve, notches);
        if (curve != current->curve)
            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetAutomationCurve>(line->id, *hit, curve), options));
        return true;
    }

    const auto value = automationEditing::stepValue(line->target, current->value, notches);
    if (value != current->value)
        static_cast<void>(bus_.execute(
            std::make_unique<domain::MoveAutomationPoint>(line->id, *hit, current->beats, value), options));
    return true;
}

void PlaylistPanel::closeAutomationWheel(bool onlyWhenRested)
{
    if (!automationWheelGesture_.has_value())
        return;
    if (onlyWhenRested && juce::Time::getMillisecondCounter() - lastAutomationWheelMs_ <= wheelRestMs)
        return;

    if (bus_.openGesture() == automationWheelGesture_)
        static_cast<void>(bus_.endGesture(*automationWheelGesture_));
    automationWheelGesture_.reset();
}

} // namespace daw::ui
