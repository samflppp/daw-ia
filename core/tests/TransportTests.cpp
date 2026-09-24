#include "TestSupport.h"
#include "daw/domain/commands/TransportCommands.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
using daw::testing::RecordingObserver;

TEST_CASE("A transient command changes the state without touching the history")
{
    Harness harness;
    RecordingObserver observer;
    harness.bus.addObserver(observer);

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());

    CHECK(harness.state.transport().playing);
    CHECK(harness.bus.undoDepth() == 0);
    CHECK_FALSE(harness.bus.canUndo());
    CHECK(harness.bus.journal().empty());

    // Still notified: the UI and MCP must see it.
    REQUIRE(observer.executed.size() == 1);
    CHECK(observer.executed.front().type == "transport.play");
    CHECK_FALSE(observer.executed.front().coalesced);

    REQUIRE(harness.bus.execute(std::make_unique<TransportStop>()).ok());
    CHECK_FALSE(harness.state.transport().playing);
    CHECK(harness.bus.undoDepth() == 0);
}

TEST_CASE("A transient command leaves the redo stack alone")
{
    Harness harness;

    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());
    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.bus.redoDepth() == 1);

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());

    // Nothing about the past has changed, so the abandoned branch survives.
    CHECK(harness.bus.redoDepth() == 1);
    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.bus.undoDepth() == 1);
}

TEST_CASE("Stopping returns the playhead to the start")
{
    Harness harness;

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetPosition>(8.0)).ok());
    CHECK(harness.state.transport().positionBeats == doctest::Approx(8.0));

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportStop>()).ok());

    // Changed in S7bis, and it contradicts the comment written in S2: a
    // beatmaker uses stop to go back to the top of the pattern, and
    // transport.set_position is what moves the playhead anywhere else --
    // including back to the start without stopping.
    CHECK_FALSE(harness.state.transport().playing);
    CHECK(harness.state.transport().positionBeats == doctest::Approx(0.0));
}

TEST_CASE("A negative playhead position is refused")
{
    Harness harness;

    CHECK(harness.bus.execute(std::make_unique<TransportSetPosition>(-1.0)).code() ==
          ErrorCode::invalidArgument);
    CHECK(harness.state.transport().positionBeats == doctest::Approx(0.0));
}

TEST_CASE("Transport is not project state")
{
    Harness harness;

    const auto before = harness.state.toValue();
    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportSetPosition>(4.0)).ok());

    // A project file does not remember that it was playing, and an undo must
    // not rewind the playhead.
    CHECK(harness.state.toValue() == before);

    const auto restored = ProjectState::fromValue(harness.state.toValue());
    REQUIRE(restored.ok());
    CHECK_FALSE(restored.value().transport().playing);
    CHECK(restored.value().transport().positionBeats == doctest::Approx(0.0));
}

TEST_CASE("Reverting a transient command is an error, not a silent no-op")
{
    ProjectState state;
    const TransportPlay command;

    const auto reverted = command.revert(state, Value{});
    REQUIRE_FALSE(reverted.ok());
    CHECK(reverted.error().code == ErrorCode::invalidArgument);
}

TEST_CASE("The transport commands survive the round-trip and are registered")
{
    const auto registry = CommandRegistry::withBuiltinCommands();

    // Nine track commands, one clip, seven note, six pattern, two
    // placement, three audio, five transport, five plugin, four tempo.
    CHECK(registry.types().size() == 42);
    CHECK(registry.contains("transport.play"));
    CHECK(registry.contains("transport.stop"));
    CHECK(registry.contains("transport.set_position"));

    const auto rebuilt =
        registry.create("transport.set_position", Value::object({{"positionBeats", Value{12.5}}}));
    REQUIRE(rebuilt.ok());
    CHECK(rebuilt.value()->historyPolicy() == HistoryPolicy::transient);
    CHECK(rebuilt.value()->payload().doubleAt("positionBeats").value() == doctest::Approx(12.5));

    CHECK(registry.create("transport.set_position", Value::object({})).code() == ErrorCode::invalidPayload);
}

TEST_CASE("The volume range is the one Tracktion actually accepts")
{
    Harness harness;

    // volumeFaderPositionToDB() tops out at +6 dB and bottoms out at -100 dB.
    CHECK(ProjectState::minVolumeDb == doctest::Approx(-100.0));
    CHECK(ProjectState::maxVolumeDb == doctest::Approx(6.0));

    CHECK(harness.bus.execute(harness.setVolume(6.0)).ok());
    CHECK(harness.bus.execute(harness.setVolume(-100.0)).ok());
    CHECK(harness.bus.execute(harness.setVolume(6.5)).code() == ErrorCode::invalidArgument);
}

TEST_CASE("switching to pattern mode names the pattern, rewinds, and writes no history")
{
    Harness harness;
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 8.0, 4.0)).ok());
    const auto patternId = ProjectState::patternIdForClip(clipId);

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetPosition>(12.0)).ok());

    const auto depth = harness.bus.undoDepth();
    const auto journalled = harness.bus.journal().size();
    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, patternId)).ok());

    CHECK(harness.state.transport().mode == PlayMode::pattern);
    CHECK(harness.state.transport().auditionedPattern == patternId);

    // Beat 12 of the song is nowhere in a four-beat pattern: the playhead goes
    // back to the start rather than to a position that means nothing.
    CHECK(harness.state.transport().positionBeats == doctest::Approx(0.0));
    CHECK(harness.bus.undoDepth() == depth);
    CHECK(harness.bus.journal().size() == journalled);

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::song, PatternId{})).ok());
    CHECK(harness.state.transport().mode == PlayMode::song);
    CHECK(harness.state.transport().auditionedPattern.isNil());
}

TEST_CASE("pattern mode may name nothing, but never a pattern that does not exist")
{
    Harness harness;

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, PatternId{})).ok());
    CHECK(harness.state.transport().mode == PlayMode::pattern);

    CHECK(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, PatternId::generate()))
              .code() == ErrorCode::notFound);

    // A refused switch leaves the mode where it was.
    CHECK(harness.state.transport().mode == PlayMode::pattern);
}

TEST_CASE("transport.set_mode reads back the payload it writes")
{
    const auto registry = CommandRegistry::withBuiltinCommands();
    const auto patternId = PatternId::generate();

    const TransportSetMode pattern{PlayMode::pattern, patternId};
    auto rebuilt = registry.create(TransportSetMode::commandType, pattern.payload());
    REQUIRE(rebuilt.ok());
    CHECK(rebuilt.value()->payload() == pattern.payload());

    const TransportSetMode song{PlayMode::song, PatternId{}};
    rebuilt = registry.create(TransportSetMode::commandType, song.payload());
    REQUIRE(rebuilt.ok());
    CHECK(rebuilt.value()->payload() == song.payload());

    CHECK_FALSE(registry
                    .create(TransportSetMode::commandType,
                            Value::object({{"mode", Value{std::string{"chanson"}}},
                                           {"patternId", Value{std::string{}}}}))
                    .ok());
}

TEST_CASE("the mode is session state: it is not serialized and survives an undo")
{
    Harness harness;
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    const auto patternId = ProjectState::patternIdForClip(clipId);

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, patternId)).ok());

    const auto restored = ProjectState::fromValue(harness.state.toValue());
    REQUIRE(restored.ok());
    CHECK(restored.value().transport().mode == PlayMode::song);

    REQUIRE(harness.bus.execute(harness.setVolume(-6.0)).ok());
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.transport().mode == PlayMode::pattern);
}
