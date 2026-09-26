#include "TestSupport.h"
#include "daw/domain/commands/AutomationCommands.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/serialization/Json.h"

#include <cmath>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

PluginInstance synth(PluginId id)
{
    PluginInstance plugin{};
    plugin.id = id;
    plugin.ref.format = std::string{PluginRef::vst3Format};
    plugin.ref.identifier = "1234567890abcdef1234567890abcdef";
    plugin.ref.name = "Synthe";
    return plugin;
}

AutomationPoint point(double beats, double value, double curve = 0.0)
{
    AutomationPoint made{};
    made.id = AutomationPointId::generate();
    made.beats = beats;
    made.value = value;
    made.curve = curve;
    return made;
}

std::unique_ptr<Command> addPoint(AutomationLineId line, const AutomationPoint& made)
{
    return std::make_unique<AddAutomationPoint>(line, made);
}

std::string snapshot(const ProjectState& state)
{
    return json::write(state.toValue());
}

} // namespace

TEST_CASE("a line is created empty, gets its points in beat order, and one per beat")
{
    Harness harness;
    const auto line = AutomationLineId::generate();
    REQUIRE(harness.bus
                .execute(
                    std::make_unique<CreateAutomationLine>(line, AutomationTarget::volumeOf(harness.trackId)))
                .ok());
    REQUIRE(harness.state.findAutomationLine(line) != nullptr);
    CHECK(harness.state.findAutomationLine(line)->points.empty());

    const auto late = point(8.0, -12.0);
    const auto early = point(0.0, 0.0);
    REQUIRE(harness.bus.execute(addPoint(line, late)).ok());
    REQUIRE(harness.bus.execute(addPoint(line, early)).ok());

    const auto& points = harness.state.findAutomationLine(line)->points;
    REQUIRE(points.size() == 2);
    CHECK(points.front().id == early.id);
    CHECK(points.back().id == late.id);

    // Refused: a second point on beat 8, a volume beyond +6 dB, a curve
    // beyond 1, a second line on the same target, a target that is not there.
    CHECK(harness.bus.execute(addPoint(line, point(8.0, -3.0))).code() == ErrorCode::invalidArgument);
    CHECK(harness.bus.execute(addPoint(line, point(4.0, 12.0))).code() == ErrorCode::invalidArgument);
    CHECK(harness.bus.execute(addPoint(line, point(4.0, 0.0, 1.5))).code() == ErrorCode::invalidArgument);
    CHECK(harness.bus
              .execute(std::make_unique<CreateAutomationLine>(AutomationLineId::generate(),
                                                              AutomationTarget::volumeOf(harness.trackId)))
              .code() == ErrorCode::conflict);
    CHECK(harness.bus
              .execute(std::make_unique<CreateAutomationLine>(AutomationLineId::generate(),
                                                              AutomationTarget::panOf(TrackId::generate())))
              .code() == ErrorCode::notFound);
    CHECK(harness.bus.undoDepth() == 3);
}

TEST_CASE("the master can be automated, like any strip")
{
    Harness harness;
    CHECK(harness.bus
              .execute(std::make_unique<CreateAutomationLine>(
                  AutomationLineId::generate(), AutomationTarget::volumeOf(ProjectState::masterTrackId())))
              .ok());
}

TEST_CASE("dragging a point is one gesture, one entry, one undo")
{
    Harness harness;
    const auto line = AutomationLineId::generate();
    const auto dragged = point(4.0, 0.0);
    REQUIRE(
        harness.bus
            .execute(std::make_unique<CreateAutomationLine>(line, AutomationTarget::panOf(harness.trackId)))
            .ok());
    REQUIRE(harness.bus.execute(addPoint(line, dragged)).ok());
    const auto before = snapshot(harness.state);
    const auto depth = harness.bus.undoDepth();

    const auto gesture = harness.bus.beginGesture("point d'automation");
    for (int step = 1; step <= 10; ++step)
    {
        REQUIRE(harness.bus
                    .execute(std::make_unique<MoveAutomationPoint>(
                                 line, dragged.id, 4.0 + step * 0.25, -0.1 * step),
                             ExecuteOptions{gesture})
                    .ok());
    }
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == depth + 1);
    const auto* moved = harness.state.findAutomationLine(line)->findPoint(dragged.id);
    CHECK(moved->beats == doctest::Approx(6.5));
    CHECK(moved->value == doctest::Approx(-1.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(snapshot(harness.state) == before);
}

TEST_CASE("a point cannot be dragged onto another one")
{
    Harness harness;
    const auto line = AutomationLineId::generate();
    const auto first = point(0.0, 0.0);
    const auto second = point(4.0, 0.5);
    REQUIRE(
        harness.bus
            .execute(std::make_unique<CreateAutomationLine>(line, AutomationTarget::panOf(harness.trackId)))
            .ok());
    REQUIRE(harness.bus.execute(addPoint(line, first)).ok());
    REQUIRE(harness.bus.execute(addPoint(line, second)).ok());

    CHECK_FALSE(harness.bus.execute(std::make_unique<MoveAutomationPoint>(line, second.id, 0.0, 0.5)).ok());

    // Past the other one is fine: the order follows.
    REQUIRE(harness.bus.execute(std::make_unique<MoveAutomationPoint>(line, first.id, 6.0, 0.0)).ok());
    CHECK(harness.state.findAutomationLine(line)->points.front().id == second.id);
}

TEST_CASE("the curve is set per point and merged per gesture")
{
    Harness harness;
    const auto line = AutomationLineId::generate();
    const auto first = point(0.0, 0.0);
    REQUIRE(
        harness.bus
            .execute(std::make_unique<CreateAutomationLine>(line, AutomationTarget::panOf(harness.trackId)))
            .ok());
    REQUIRE(harness.bus.execute(addPoint(line, first)).ok());
    const auto depth = harness.bus.undoDepth();

    const auto gesture = harness.bus.beginGesture("courbe");
    for (const auto curve : {0.1, 0.2, 0.3, -0.4})
        REQUIRE(
            harness.bus
                .execute(std::make_unique<SetAutomationCurve>(line, first.id, curve), ExecuteOptions{gesture})
                .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == depth + 1);
    CHECK(harness.state.findAutomationLine(line)->points.front().curve == doctest::Approx(-0.4));
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findAutomationLine(line)->points.front().curve == 0.0);
}

TEST_CASE("the value between two points is the one Tracktion plays")
{
    AutomationLine line{};
    line.id = AutomationLineId::generate();
    line.target = AutomationTarget::panOf(TrackId::generate());
    line.points = {point(0.0, -1.0), point(4.0, 1.0)};

    // Held before the first point and after the last.
    CHECK(line.valueAt(-1.0) == doctest::Approx(-1.0));
    CHECK(line.valueAt(12.0) == doctest::Approx(1.0));

    // Straight: halfway is halfway.
    CHECK(line.valueAt(2.0) == doctest::Approx(0.0));

    // Bent: a positive curve pulls the middle toward the end value, a
    // negative one toward the start value, and the ends stay where they are.
    line.points.front().curve = 0.5;
    const auto bentUp = line.valueAt(2.0);
    line.points.front().curve = -0.5;
    const auto bentDown = line.valueAt(2.0);
    CHECK(bentUp != doctest::Approx(0.0));
    CHECK(bentDown != doctest::Approx(0.0));
    CHECK((bentUp > 0.0) != (bentDown > 0.0));
    CHECK(line.valueAt(0.0) == doctest::Approx(-1.0));
    CHECK(line.valueAt(4.0) == doctest::Approx(1.0));

    // Past a half, one end holds flat for a while.
    line.points.front().curve = -1.0;
    CHECK(line.valueAt(3.9) == doctest::Approx(1.0));

    // The volume bends in fader position, not in dB: halfway between 0 and
    // -100 dB is the position halfway to silence, about -13.9 dB, not -50.
    AutomationLine volume{};
    volume.target = AutomationTarget::volumeOf(TrackId::generate());
    volume.points = {point(0.0, 0.0), point(4.0, ProjectState::minVolumeDb)};
    const auto halfway = volume.valueAt(2.0);
    CHECK(halfway == doctest::Approx(20.0 * std::log(0.5 * std::exp(-6.0 / 20.0)) + 6.0));
    CHECK(halfway > -20.0);
}

TEST_CASE("automation.write replaces a range in one entry, creating the line when needed")
{
    Harness harness;
    const auto master = AutomationTarget::volumeOf(ProjectState::masterTrackId());
    const auto line = AutomationLineId::generate();
    const auto before = snapshot(harness.state);

    // The fade-out of the copilot: two points, one entry.
    REQUIRE(harness.bus
                .execute(std::make_unique<WriteAutomation>(
                    line,
                    master,
                    48.0,
                    64.0,
                    std::vector<AutomationPoint>{point(48.0, 0.0), point(64.0, -100.0)}))
                .ok());
    CHECK(harness.bus.undoDepth() == 1);
    REQUIRE(harness.state.findAutomationLineFor(master) != nullptr);
    CHECK(harness.state.findAutomationLineFor(master)->points.size() == 2);

    // Written again over part of it: the points inside go, the others stay,
    // and the line is the same one even though another identifier is given.
    const auto after = snapshot(harness.state);
    REQUIRE(harness.bus
                .execute(std::make_unique<WriteAutomation>(AutomationLineId::generate(),
                                                           master,
                                                           56.0,
                                                           64.0,
                                                           std::vector<AutomationPoint>{point(60.0, -20.0)}))
                .ok());
    const auto* written = harness.state.findAutomationLineFor(master);
    REQUIRE(written != nullptr);
    CHECK(written->id == line);
    REQUIRE(written->points.size() == 2);
    CHECK(written->points.back().beats == 60.0);

    // Refused whole: a point outside the range, a range backwards.
    CHECK(harness.bus
              .execute(std::make_unique<WriteAutomation>(
                  line, master, 0.0, 4.0, std::vector<AutomationPoint>{point(8.0, 0.0)}))
              .code() == ErrorCode::invalidArgument);
    CHECK(harness.bus
              .execute(
                  std::make_unique<WriteAutomation>(line, master, 8.0, 4.0, std::vector<AutomationPoint>{}))
              .code() == ErrorCode::invalidArgument);

    REQUIRE(harness.bus.undo().ok());
    CHECK(snapshot(harness.state) == after);
    REQUIRE(harness.bus.undo().ok());
    CHECK(snapshot(harness.state) == before);
}

TEST_CASE("removing a plugin takes the lines of its parameters, and the undo gives them back")
{
    Harness harness;
    const auto pluginId = PluginId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, synth(pluginId), 0)).ok());

    const auto volume = AutomationLineId::generate();
    const auto cutoff = AutomationLineId::generate();
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(volume,
                                                                AutomationTarget::volumeOf(harness.trackId)))
                .ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(
                    cutoff, AutomationTarget::parameterOf(pluginId, "cutoff")))
                .ok());
    REQUIRE(harness.bus.execute(addPoint(cutoff, point(0.0, 0.2))).ok());
    const auto before = snapshot(harness.state);

    REQUIRE(harness.bus.execute(std::make_unique<RemovePlugin>(pluginId)).ok());
    CHECK(harness.state.findAutomationLine(cutoff) == nullptr);
    CHECK(harness.state.findAutomationLine(volume) != nullptr);

    REQUIRE(harness.bus.undo().ok());
    CHECK(snapshot(harness.state) == before);
    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.findAutomationLine(cutoff) == nullptr);
}

TEST_CASE("removing a track or a bus takes every line aimed at it, and the undo gives them back")
{
    Harness harness;
    const auto pluginId = PluginId::generate();
    const auto busId = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, synth(pluginId), 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddBus>(busId, "Reverb")).ok());

    // The master first, so the track's lines are in the middle of the list
    // and have to come back to their ranks, not to the end.
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(
                    AutomationLineId::generate(), AutomationTarget::volumeOf(ProjectState::masterTrackId())))
                .ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(AutomationLineId::generate(),
                                                                AutomationTarget::panOf(harness.trackId)))
                .ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(
                    AutomationLineId::generate(), AutomationTarget::parameterOf(pluginId, "cutoff")))
                .ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(AutomationLineId::generate(),
                                                                AutomationTarget::volumeOf(busId)))
                .ok());
    const auto before = snapshot(harness.state);

    REQUIRE(harness.bus.execute(std::make_unique<RemoveTrack>(harness.trackId)).ok());
    CHECK(harness.state.automation().size() == 2);
    REQUIRE(harness.bus.execute(std::make_unique<RemoveTrack>(busId)).ok());
    CHECK(harness.state.automation().size() == 1);

    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.bus.undo().ok());
    CHECK(snapshot(harness.state) == before);
}

TEST_CASE("a tempo change moves no automation point")
{
    Harness harness;
    const auto line = AutomationLineId::generate();
    REQUIRE(harness.bus
                .execute(std::make_unique<WriteAutomation>(
                    line,
                    AutomationTarget::panOf(harness.trackId),
                    0.0,
                    16.0,
                    std::vector<AutomationPoint>{point(0.0, -1.0), point(16.0, 1.0)}))
                .ok());
    const auto points = harness.state.findAutomationLine(line)->points;

    REQUIRE(harness.bus.execute(std::make_unique<SetTempoPointBpm>(ProjectState::originTempoPointId(), 90.0))
                .ok());
    CHECK(harness.state.findAutomationLine(line)->points == points);
}

TEST_CASE("automation survives serialisation, and a project without it serialises as before")
{
    Harness harness;
    const auto withoutAutomation = harness.state.toValue();
    CHECK(withoutAutomation.find("automation") == nullptr);

    const auto pluginId = PluginId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, synth(pluginId), 0)).ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<WriteAutomation>(
                    AutomationLineId::generate(),
                    AutomationTarget::parameterOf(pluginId, "cutoff"),
                    0.0,
                    8.0,
                    std::vector<AutomationPoint>{point(0.0, 0.1, 0.3), point(8.0, 0.9)}))
                .ok());

    const auto text = json::write(harness.state.toValue());
    const auto read = json::read(text);
    REQUIRE(read.ok());
    const auto rebuilt = ProjectState::fromValue(read.value());
    REQUIRE(rebuilt.ok());
    CHECK(rebuilt.value() == harness.state);
}

TEST_CASE("the automation commands replay from their payloads to the same project")
{
    Harness recorded;
    const auto line = AutomationLineId::generate();
    const auto first = point(0.0, 0.0);
    REQUIRE(
        recorded.bus
            .execute(std::make_unique<CreateAutomationLine>(line, AutomationTarget::panOf(recorded.trackId)))
            .ok());
    REQUIRE(recorded.bus.execute(addPoint(line, first)).ok());
    REQUIRE(recorded.bus.execute(addPoint(line, point(4.0, 1.0))).ok());
    REQUIRE(recorded.bus.execute(std::make_unique<MoveAutomationPoint>(line, first.id, 1.0, -0.5)).ok());
    REQUIRE(recorded.bus.execute(std::make_unique<SetAutomationCurve>(line, first.id, 0.25)).ok());
    REQUIRE(recorded.bus.execute(std::make_unique<RemoveAutomationPoint>(line, first.id)).ok());

    Harness replayed;
    Track track{};
    track.id = recorded.trackId;
    track.name = "Piste 1";
    REQUIRE(replayed.state.addTrack(track).ok());
    // Both harnesses built a track of their own in the constructor; only the
    // shared one is compared.
    for (const auto& envelope : recorded.bus.journal())
        REQUIRE(replayed.bus.executeSerialized(envelope).ok());

    REQUIRE(replayed.state.findAutomationLine(line) != nullptr);
    CHECK(*replayed.state.findAutomationLine(line) == *recorded.state.findAutomationLine(line));
}
