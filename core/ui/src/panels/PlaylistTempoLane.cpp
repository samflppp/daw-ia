// The playlist's tempo lane: FL's tempo automation clip, drawn over the
// timeline under the ruler.
//
// It shows the project's tempo sequence as it is — a step line, since the
// domain's tempo points hold their value until the next one — and edits it
// with the four tempo commands a copilot uses too. It is shown as soon as the
// tempo changes somewhere past the origin, and goes away with the last change:
// there is no lane of its own to create or to delete, the sequence is the
// automation.
//
//   click on the lane          a change at the bar under the pointer (the
//                              beat with Shift), at the tempo under it
//   drag a point               up and down: its tempo; sideways: where it
//                              starts. The first movement picks the axis, so
//                              a drag is one history entry
//   right-click a point        removes it
//   double-click a point       type its tempo
//   wheel over the lane        the tempo in force under the pointer, one BPM
//                              a notch
// The point at the origin is the project's tempo: it can be dragged up and
// down, never sideways, and never removed.

#include "daw/domain/commands/TempoCommands.h"
#include "daw/ui/model/TempoEditing.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace daw::ui
{
namespace
{

// How long the wheel rests before its turn is over.
constexpr juce::uint32 wheelRestMs = 500;

// Room above and below the extreme tempos, so a point is never drawn on the
// edge of the lane, and the step the range is rounded to.
constexpr double tempoMargin = 20.0;
constexpr double tempoRangeStep = 10.0;

} // namespace

int PlaylistPanel::tempoLaneHeight() const
{
    return tempoEditing::isAutomated(state_) ? tokens_.integer("metric.playlist.tempoLaneHeight") : 0;
}

juce::Rectangle<int> PlaylistPanel::tempoLaneArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                       tokens_.integer("metric.playlist.rulerHeight"));
    area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    area.removeFromRight(tokens_.integer("metric.scrollbar.thickness"));
    return area.removeFromTop(tempoLaneHeight());
}

juce::Rectangle<int> PlaylistPanel::tempoHeaderArea() const
{
    const auto lane = tempoLaneArea();
    return {0, lane.getY(), tokens_.integer("metric.playlist.headerWidth"), lane.getHeight()};
}

PlaylistPanel::TempoRange PlaylistPanel::tempoRange() const
{
    const auto& points = state_.tempoPoints();
    const auto [lowest, highest] =
        std::minmax_element(points.begin(),
                            points.end(),
                            [](const domain::TempoPoint& lhs, const domain::TempoPoint& rhs)
                            { return lhs.beatsPerMinute < rhs.beatsPerMinute; });

    TempoRange range;
    range.low =
        std::max(domain::ProjectState::minTempo,
                 std::floor((lowest->beatsPerMinute - tempoMargin) / tempoRangeStep) * tempoRangeStep);
    range.high =
        std::min(domain::ProjectState::maxTempo,
                 std::ceil((highest->beatsPerMinute + tempoMargin) / tempoRangeStep) * tempoRangeStep);
    return range;
}

int PlaylistPanel::yForTempo(double bpm, TempoRange range) const
{
    const auto handle = tokens_.integer("metric.playlist.tempoHandle");
    const auto lane = tempoLaneArea().withTrimmedTop(handle).withTrimmedBottom(handle);
    const auto amount = (bpm - range.low) / std::max(1.0, range.high - range.low);
    return lane.getBottom() - static_cast<int>(std::lround(amount * lane.getHeight()));
}

double PlaylistPanel::tempoAtY(int y, TempoRange range) const
{
    const auto handle = tokens_.integer("metric.playlist.tempoHandle");
    const auto lane = tempoLaneArea().withTrimmedTop(handle).withTrimmedBottom(handle);
    const auto amount = static_cast<double>(lane.getBottom() - y) / std::max(1, lane.getHeight());
    return std::clamp(std::round(range.low + amount * (range.high - range.low)),
                      domain::ProjectState::minTempo,
                      domain::ProjectState::maxTempo);
}

juce::Point<int> PlaylistPanel::tempoPointFor(double beats, double bpm) const
{
    return {xForBeat(beats), yForTempo(bpm, tempoRange())};
}

std::optional<domain::TempoPointId> PlaylistPanel::tempoPointAt(juce::Point<int> point) const
{
    if (!tempoLaneArea().contains(point))
        return {};

    // The handle, and a little around it: a point is small, a hand is not.
    const auto reach = tokens_.integer("metric.playlist.tempoHandle") * 2;
    const auto range = tempoRange();

    std::optional<domain::TempoPointId> nearest;
    auto best = reach + 1;
    for (const auto& tempo : state_.tempoPoints())
    {
        const auto handle =
            juce::Point<int>{xForBeat(tempo.startBeats), yForTempo(tempo.beatsPerMinute, range)};
        const auto distance = static_cast<int>(std::lround(handle.getDistanceFrom(point)));
        if (distance <= reach && distance < best)
        {
            best = distance;
            nearest = tempo.id;
        }
    }
    return nearest;
}

void PlaylistPanel::paintTempoLane(juce::Graphics& g) const
{
    const auto lane = tempoLaneArea();
    if (lane.isEmpty())
        return;

    const auto hairline = tokens_.integer("stroke.hairline");
    const auto header = tempoHeaderArea();

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(header);
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText(
        "TEMPO", header.reduced(tokens_.integer("space.sm"), 0), juce::Justification::centredLeft, false);

    g.saveState();
    g.reduceClipRegion(lane);

    const auto range = tempoRange();
    const auto& points = state_.tempoPoints();
    const auto right = lane.getRight();

    // The step line: each tempo held until the next change, the area under it
    // shaded so the eye reads the level before the numbers.
    juce::Path line;
    juce::Path area;
    area.startNewSubPath(static_cast<float>(xForBeat(0.0)), static_cast<float>(lane.getBottom()));
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const auto x = static_cast<float>(xForBeat(points[index].startBeats));
        const auto y = static_cast<float>(yForTempo(points[index].beatsPerMinute, range));
        const auto next = index + 1 < points.size()
                              ? static_cast<float>(xForBeat(points[index + 1].startBeats))
                              : static_cast<float>(right);
        if (index == 0)
            line.startNewSubPath(x, y);
        else
            line.lineTo(x, y);
        line.lineTo(next, y);
        area.lineTo(x, y);
        area.lineTo(next, y);
    }
    area.lineTo(static_cast<float>(right), static_cast<float>(lane.getBottom()));
    area.closeSubPath();

    g.setColour(tokens_.colour("color.state.selected"));
    g.fillPath(area);
    g.setColour(tokens_.colour("color.accent.primary"));
    g.strokePath(line, juce::PathStrokeType{tokens_.number("stroke.focus")});

    const auto radius = tokens_.number("metric.playlist.tempoHandle");
    g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
    for (const auto& tempo : points)
    {
        const auto handle =
            juce::Point<int>{xForBeat(tempo.startBeats), yForTempo(tempo.beatsPerMinute, range)};
        g.setColour(tokens_.colour("color.accent.primary"));
        g.fillEllipse(juce::Rectangle<float>{radius * 2.0f, radius * 2.0f}.withCentre(handle.toFloat()));

        const auto bpm = tempo.beatsPerMinute;
        const auto text = juce::String(bpm, std::abs(bpm - std::round(bpm)) < 1e-9 ? 0 : 1);
        const auto labelHeight = tokens_.integer("metric.playlist.labelHeight");
        const auto above = handle.getY() - labelHeight - static_cast<int>(radius) >= lane.getY();
        g.setColour(tokens_.colour("color.text.secondary"));
        g.drawText(
            text,
            juce::Rectangle<int>{handle.getX() + static_cast<int>(radius) + tokens_.integer("space.xs"),
                                 above ? handle.getY() - labelHeight - static_cast<int>(radius)
                                       : handle.getY() + static_cast<int>(radius),
                                 tokens_.integer("space.xl") * 2,
                                 labelHeight},
            juce::Justification::centredLeft,
            false);
    }

    g.restoreState();

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.getX(), lane.getBottom() - hairline, lane.getRight() - header.getX(), hairline);
    g.fillRect(header.getRight() - hairline, header.getY(), hairline, header.getHeight());
}

bool PlaylistPanel::tempoMouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    if (!tempoLaneArea().contains(point))
        return false;

    closeTempoWheel();
    auto hit = tempoPointAt(point);

    if (event.mods.isRightButtonDown())
    {
        if (hit.has_value() && *hit != domain::ProjectState::originTempoPointId())
            static_cast<void>(bus_.execute(std::make_unique<domain::RemoveTempoPoint>(*hit)));
        return true;
    }

    const auto range = tempoRange();

    if (!hit.has_value())
    {
        // A new change where the hand is, then dragged like any other: a
        // click that places and a drag that adjusts are one gesture in FL.
        const auto beats = snap(beatAtX(point.getX()), event.mods.isShiftDown());
        if (beats <= 0.0)
            return true;

        const auto pointId = domain::TempoPointId::generate();
        if (!bus_.execute(std::make_unique<domain::InsertTempoPoint>(
                              pointId, beats, tempoAtY(point.getY(), range)))
                 .ok())
            return true;
        hit = pointId;
    }

    const auto* grabbed = state_.findTempoPoint(*hit);
    if (grabbed == nullptr)
        return true;

    TempoDrag drag;
    drag.pointId = *hit;
    drag.grab = point;
    drag.grabBpm = grabbed->beatsPerMinute;
    drag.bpmPerPixel =
        (range.high - range.low) /
        std::max(1, tempoLaneArea().getHeight() - 2 * tokens_.integer("metric.playlist.tempoHandle"));
    drag.gesture = bus_.beginGesture("tempo au trait");
    tempoDrag_ = drag;
    return true;
}

bool PlaylistPanel::tempoMouseDrag(const juce::MouseEvent& event)
{
    if (!tempoDrag_.has_value())
        return false;

    auto& drag = *tempoDrag_;
    const auto point = event.getPosition();
    const auto moved = point - drag.grab;

    // The first real movement picks the axis. Tempo and position in one drag
    // would alternate two commands, and the history would keep every turn.
    if (drag.axis == TempoDrag::Axis::none)
    {
        const auto threshold = tokens_.integer("metric.playlist.tempoHandle");
        if (std::abs(moved.getX()) < threshold && std::abs(moved.getY()) < threshold)
            return true;
        const auto sideways = std::abs(moved.getX()) > std::abs(moved.getY()) &&
                              drag.pointId != domain::ProjectState::originTempoPointId();
        drag.axis = sideways ? TempoDrag::Axis::position : TempoDrag::Axis::tempo;
    }

    const auto* current = state_.findTempoPoint(drag.pointId);
    if (current == nullptr)
        return true;

    domain::ExecuteOptions options;
    options.gesture = drag.gesture;

    if (drag.axis == TempoDrag::Axis::tempo)
    {
        const auto wanted = std::clamp(std::round(drag.grabBpm - moved.getY() * drag.bpmPerPixel),
                                       domain::ProjectState::minTempo,
                                       domain::ProjectState::maxTempo);
        if (wanted != current->beatsPerMinute)
            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetTempoPointBpm>(drag.pointId, wanted), options));
        return true;
    }

    // A point never reaches the origin, and never lands on another one: the
    // command would refuse it, and the point stays where it last could be.
    const auto beats = snap(beatAtX(point.getX()), event.mods.isShiftDown());
    if (beats > 0.0 && beats != current->startBeats)
        static_cast<void>(
            bus_.execute(std::make_unique<domain::MoveTempoPoint>(drag.pointId, beats), options));
    return true;
}

bool PlaylistPanel::tempoMouseUp()
{
    if (!tempoDrag_.has_value())
        return false;

    if (bus_.openGesture() == tempoDrag_->gesture)
        static_cast<void>(bus_.endGesture(tempoDrag_->gesture));
    tempoDrag_.reset();
    return true;
}

bool PlaylistPanel::tempoDoubleClick(juce::Point<int> point)
{
    const auto hit = tempoPointAt(point);
    if (!hit.has_value())
        return tempoLaneArea().contains(point);

    const auto* tempo = state_.findTempoPoint(*hit);
    if (tempo == nullptr)
        return true;

    const auto bpm = tempo->beatsPerMinute;
    auto* window = new juce::AlertWindow(
        u8"Changement de tempo", u8"En BPM, de 20 à 300.", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("tempo", juce::String(bpm, std::abs(bpm - std::round(bpm)) < 1e-9 ? 0 : 1));
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<PlaylistPanel> safe{this};
    const auto pointId = *hit;
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe, window, pointId](int result)
                                {
                                    if (safe == nullptr || result != 1)
                                        return;

                                    const auto typed = tempoEditing::parseTempo(
                                        window->getTextEditorContents("tempo").toStdString());
                                    if (typed.has_value())
                                        static_cast<void>(safe->bus_.execute(
                                            std::make_unique<domain::SetTempoPointBpm>(pointId, *typed)));
                                }),
                            true);
    return true;
}

bool PlaylistPanel::tempoWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!tempoLaneArea().contains(event.getPosition()) || event.mods.isCtrlDown() ||
        event.mods.isShiftDown() || wheel.deltaX != 0.0f)
        return false;

    const auto notches = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (notches == 0)
        return true;

    // The change in force under the pointer: the last one at or before it.
    const auto beats = beatAtX(event.getPosition().getX());
    const domain::TempoPoint* inForce = &state_.tempoPoints().front();
    for (const auto& tempo : state_.tempoPoints())
    {
        if (tempo.startBeats <= beats)
            inForce = &tempo;
    }

    const auto wanted = tempoEditing::stepTempo(inForce->beatsPerMinute, notches);
    if (wanted == inForce->beatsPerMinute)
        return true;

    lastTempoWheelMs_ = juce::Time::getMillisecondCounter();
    if (!tempoWheelGesture_.has_value() || bus_.openGesture() != tempoWheelGesture_)
        tempoWheelGesture_ = bus_.beginGesture("molette sur le tempo");

    domain::ExecuteOptions options;
    options.gesture = tempoWheelGesture_;
    static_cast<void>(bus_.execute(std::make_unique<domain::SetTempoPointBpm>(inForce->id, wanted), options));
    return true;
}

void PlaylistPanel::closeTempoWheel(bool onlyWhenRested)
{
    if (!tempoWheelGesture_.has_value())
        return;
    if (onlyWhenRested && juce::Time::getMillisecondCounter() - lastTempoWheelMs_ <= wheelRestMs)
        return;

    if (bus_.openGesture() == tempoWheelGesture_)
        static_cast<void>(bus_.endGesture(*tempoWheelGesture_));
    tempoWheelGesture_.reset();
}

} // namespace daw::ui
