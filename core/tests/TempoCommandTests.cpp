#include "TestSupport.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/serialization/Json.h"

#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

TempoPointId origin()
{
    return ProjectState::originTempoPointId();
}

} // namespace

TEST_CASE("a tempo change is a command, so a journal can bring it back")
{
    Harness harness;
    const auto pointId = TempoPointId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(pointId, 8.0, 140.0)).ok());
    CHECK(harness.state.tempoAt(8.0) == doctest::Approx(140.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.tempoPoints().size() == 1);
    CHECK(harness.state.tempoAt(8.0) == doctest::Approx(120.0));

    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.tempoAt(8.0) == doctest::Approx(140.0));
}

TEST_CASE("undoing a removal puts the point back with its beat and its tempo")
{
    Harness harness;
    const auto pointId = TempoPointId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(pointId, 8.0, 140.0)).ok());

    const auto before = harness.state.toValue();

    REQUIRE(harness.bus.execute(std::make_unique<RemoveTempoPoint>(pointId)).ok());
    CHECK(harness.state.tempoPoints().size() == 1);

    REQUIRE(harness.bus.undo().ok());

    // Not "a point at 8 beats and 140": the same sequence, byte for byte. The
    // undo record carries the whole point because the command knows none of it.
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("the point at the origin refuses to be removed or moved, and leaves no history")
{
    Harness harness;

    auto removed = harness.bus.execute(std::make_unique<RemoveTempoPoint>(origin()));
    CHECK(removed.error().code == ErrorCode::invalidArgument);

    auto moved = harness.bus.execute(std::make_unique<MoveTempoPoint>(origin(), 4.0));
    CHECK(moved.error().code == ErrorCode::invalidArgument);

    // A refused command creates no entry: the state was never touched.
    CHECK(harness.bus.undoDepth() == 0);
    CHECK(harness.state.tempoPoints().size() == 1);
}

TEST_CASE("the tempo of the origin changes like any other")
{
    Harness harness;

    REQUIRE(harness.bus.execute(std::make_unique<SetTempoPointBpm>(origin(), 93.0)).ok());
    CHECK(harness.state.tempoAt(0.0) == doctest::Approx(93.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.tempoAt(0.0) == doctest::Approx(120.0));
}

TEST_CASE("a tempo drag is one history entry, and two points stay two")
{
    Harness harness;
    const auto second = TempoPointId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(second, 8.0, 140.0)).ok());

    const auto before = harness.bus.undoDepth();

    const auto gesture = harness.bus.beginGesture("tempo");
    for (int frame = 0; frame < 20; ++frame)
    {
        const auto bpm = 120.0 + static_cast<double>(frame);
        REQUIRE(
            harness.bus.execute(std::make_unique<SetTempoPointBpm>(origin(), bpm), ExecuteOptions{gesture})
                .ok());
    }

    // Another point inside the same gesture: undoing one must not move the
    // other, so it is its own entry.
    REQUIRE(
        harness.bus.execute(std::make_unique<SetTempoPointBpm>(second, 90.0), ExecuteOptions{gesture}).ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == before + 2);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.tempoAt(8.0) == doctest::Approx(140.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.tempoAt(0.0) == doctest::Approx(120.0));
}

TEST_CASE("moving a point does not change its tempo, and coalesces per point")
{
    Harness harness;
    const auto pointId = TempoPointId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(pointId, 8.0, 140.0)).ok());

    const auto before = harness.bus.undoDepth();

    const auto gesture = harness.bus.beginGesture("deplacement du tempo");
    for (int frame = 1; frame <= 10; ++frame)
        REQUIRE(harness.bus
                    .execute(std::make_unique<MoveTempoPoint>(pointId, static_cast<double>(frame)),
                             ExecuteOptions{gesture})
                    .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == before + 1);

    const auto* point = harness.state.findTempoPoint(pointId);
    REQUIRE(point != nullptr);
    CHECK(point->startBeats == doctest::Approx(10.0));
    CHECK(point->beatsPerMinute == doctest::Approx(140.0));

    // One undo for the whole drag, and it lands on the beat the point left.
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findTempoPoint(pointId)->startBeats == doctest::Approx(8.0));
}

TEST_CASE("two tempo points cannot land on the same beat, whichever verb tries")
{
    Harness harness;
    const auto first = TempoPointId::generate();
    const auto second = TempoPointId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(first, 8.0, 140.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(second, 16.0, 90.0)).ok());

    CHECK(harness.bus.execute(std::make_unique<InsertTempoPoint>(TempoPointId::generate(), 8.0, 60.0))
              .error()
              .code == ErrorCode::conflict);
    CHECK(harness.bus.execute(std::make_unique<MoveTempoPoint>(second, 8.0)).error().code ==
          ErrorCode::conflict);
    CHECK(harness.bus.execute(std::make_unique<MoveTempoPoint>(second, 0.0)).error().code ==
          ErrorCode::invalidArgument);

    CHECK(harness.state.tempoPoints().size() == 3);
    CHECK(harness.bus.undoDepth() == 2);
}

TEST_CASE("a tempo command is rebuilt from its payload alone")
{
    // No Harness here: its track is put into the state directly, and this test
    // needs a project every line of which came from the journal.
    ProjectState recorded;
    const auto registry = CommandRegistry::withBuiltinCommands();
    CommandBus recording{recorded, registry};
    const auto pointId = TempoPointId::generate();

    const auto gesture = recording.beginGesture("tempo");
    REQUIRE(recording.execute(std::make_unique<InsertTempoPoint>(pointId, 8.0, 140.0)).ok());
    for (int frame = 0; frame < 5; ++frame)
        REQUIRE(recording
                    .execute(std::make_unique<SetTempoPointBpm>(pointId, 140.0 + static_cast<double>(frame)),
                             ExecuteOptions{gesture})
                    .ok());
    REQUIRE(recording.endGesture(gesture).ok());
    REQUIRE(recording.execute(std::make_unique<MoveTempoPoint>(pointId, 12.0)).ok());
    REQUIRE(recording.execute(std::make_unique<SetTempoPointBpm>(origin(), 93.0)).ok());

    std::vector<std::string> lines;
    for (const auto& envelope : recording.journal())
        lines.push_back(json::write(envelope));

    // A fresh project, and nothing but the text of the journal: this is what a
    // reopened project does, and what a copilot will do over JSON-RPC.
    ProjectState replayed;
    CommandBus bus{replayed, registry};

    for (const auto& line : lines)
    {
        const auto envelope = json::read(line);
        REQUIRE(envelope.ok());
        REQUIRE(bus.executeSerialized(envelope.value()).ok());
    }

    CHECK(replayed == recorded);
}

TEST_CASE("a tempo payload is refused rather than guessed at")
{
    const auto registry = CommandRegistry::withBuiltinCommands();

    CHECK(registry.create(InsertTempoPoint::commandType, Value::object({{"startBeats", Value{8.0}}}))
              .error()
              .code == ErrorCode::invalidPayload);

    CHECK(registry
              .create(SetTempoPointBpm::commandType,
                      Value::object({{"pointId", Value{std::string{"pas un identifiant"}}},
                                     {"beatsPerMinute", Value{140.0}}}))
              .error()
              .code == ErrorCode::invalidPayload);

    // Out of Tracktion's range: the command is built, and refused on apply.
    ProjectState state;
    const auto registry2 = CommandRegistry::withBuiltinCommands();
    CommandBus bus{state, registry2};
    CHECK(
        bus.execute(std::make_unique<InsertTempoPoint>(TempoPointId::generate(), 8.0, 301.0)).error().code ==
        ErrorCode::invalidArgument);
}

TEST_CASE("the four tempo verbs are in the registry, under the names a caller sees")
{
    const auto registry = CommandRegistry::withBuiltinCommands();

    CHECK(registry.contains("tempo.insert"));
    CHECK(registry.contains("tempo.remove"));
    CHECK(registry.contains("tempo.set_bpm"));
    CHECK(registry.contains("tempo.move"));
}

TEST_CASE("the time signature is a command: undone, redone, and a wheel turn is one entry")
{
    Harness harness;
    CHECK(harness.state.timeSignature() == TimeSignature{});
    CHECK(harness.state.beatsPerBar() == doctest::Approx(4.0));

    const auto before = harness.bus.undoDepth();
    const auto gesture = harness.bus.beginGesture("molette sur la signature");
    for (int numerator = 5; numerator <= 7; ++numerator)
        REQUIRE(harness.bus
                    .execute(std::make_unique<SetTimeSignature>(TimeSignature{numerator, 8}),
                             ExecuteOptions{gesture})
                    .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == before + 1);
    CHECK(harness.state.timeSignature() == TimeSignature{7, 8});
    // A beat stays a quarter note: seven eighths are three and a half beats.
    CHECK(harness.state.beatsPerBar() == doctest::Approx(3.5));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.timeSignature() == TimeSignature{});
    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.timeSignature() == TimeSignature{7, 8});
}

TEST_CASE("a time signature out of range is refused and leaves no history")
{
    Harness harness;
    const auto before = harness.bus.undoDepth();

    for (const auto wrong :
         {TimeSignature{0, 4}, TimeSignature{17, 4}, TimeSignature{4, 3}, TimeSignature{4, 32}})
        CHECK(harness.bus.execute(std::make_unique<SetTimeSignature>(wrong)).error().code ==
              ErrorCode::invalidArgument);

    CHECK(harness.bus.undoDepth() == before);
    CHECK(harness.state.timeSignature() == TimeSignature{});
}

TEST_CASE("4/4 is not written, any other signature survives the round-trip")
{
    ProjectState untouched;
    CHECK(untouched.toValue().find("timeSignature") == nullptr);

    ProjectState state;
    REQUIRE(state.setTimeSignature(TimeSignature{6, 8}).ok());
    const auto text = json::write(state.toValue());
    auto parsed = json::read(text);
    REQUIRE(parsed.ok());
    auto reread = ProjectState::fromValue(parsed.value());
    REQUIRE(reread.ok());
    CHECK(reread.value().timeSignature() == TimeSignature{6, 8});
    CHECK(reread.value() == state);

    auto command = CommandRegistry::withBuiltinCommands().create(
        "project.set_time_signature", Value::object({{"numerator", Value{3}}, {"denominator", Value{4}}}));
    REQUIRE(command.ok());
    CHECK(command.value()->payload() == TimeSignature{3, 4}.toValue());
}
