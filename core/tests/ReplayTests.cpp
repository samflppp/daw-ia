#include "TestSupport.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/serialization/Json.h"

#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

// Builds a small session: a clip, three notes, and a fader drag coalesced into
// a single entry. Returns the journal as text, the way a file would hold it.
std::vector<std::string> recordSession(Harness& harness)
{
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 8.0)).ok());
    REQUIRE(harness.bus.execute(Harness::addNote(clipId, NoteId::generate(), 60)).ok());
    REQUIRE(harness.bus.execute(Harness::addNote(clipId, NoteId::generate(), 64)).ok());
    REQUIRE(harness.bus.execute(Harness::addNote(clipId, NoteId::generate(), 67)).ok());

    const auto gesture = harness.bus.beginGesture("fader volume");
    for (int frame = 0; frame < 30; ++frame)
        REQUIRE(
            harness.bus.execute(harness.setVolume(-0.2 * static_cast<double>(frame)), ExecuteOptions{gesture})
                .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    std::vector<std::string> lines;
    for (const auto& envelope : harness.bus.journal())
        lines.push_back(json::write(envelope));
    return lines;
}

} // namespace

TEST_CASE("Replaying the journal rebuilds the same project")
{
    Harness recorded;
    const auto lines = recordSession(recorded);

    // 34 commands were executed; the drag is one entry, so the journal is 5.
    CHECK(lines.size() == 5);

    // A fresh project, same starting point, nothing shared with the first one.
    Harness replayed;
    Track track{};
    track.id = recorded.trackId;
    track.name = "Piste 1";
    REQUIRE(replayed.state.addTrack(track).ok());

    for (const auto& line : lines)
    {
        const auto value = json::read(line);
        REQUIRE(value.ok());
        REQUIRE(replayed.bus.executeSerialized(value.value()).ok());
    }

    // The replayed project holds the track it started with plus the one the
    // harness created; compare the recorded track only.
    const auto* original = recorded.state.findTrack(recorded.trackId);
    const auto* copy = replayed.state.findTrack(recorded.trackId);
    REQUIRE(original != nullptr);
    REQUIRE(copy != nullptr);
    CHECK(original->toValue() == copy->toValue());
}

TEST_CASE("A replayed history keeps the identity of the commands")
{
    Harness recorded;
    const auto lines = recordSession(recorded);

    Harness replayed;
    Track track{};
    track.id = recorded.trackId;
    track.name = "Piste 1";
    REQUIRE(replayed.state.addTrack(track).ok());

    for (const auto& line : lines)
    {
        const auto value = json::read(line);
        REQUIRE(value.ok());
        REQUIRE(replayed.bus.executeSerialized(value.value()).ok());
    }

    const auto before = recorded.bus.journal();
    const auto after = replayed.bus.journal();
    REQUIRE(before.size() == after.size());

    for (std::size_t index = 0; index < before.size(); ++index)
        CHECK(before[index] == after[index]);
}

TEST_CASE("A replayed project can still be undone")
{
    Harness recorded;
    const auto lines = recordSession(recorded);

    Harness replayed;
    Track track{};
    track.id = recorded.trackId;
    track.name = "Piste 1";
    REQUIRE(replayed.state.addTrack(track).ok());

    for (const auto& line : lines)
    {
        const auto value = json::read(line);
        REQUIRE(value.ok());
        REQUIRE(replayed.bus.executeSerialized(value.value()).ok());
    }

    CHECK(replayed.bus.undoDepth() == lines.size());

    while (replayed.bus.canUndo())
        REQUIRE(replayed.bus.undo().ok());

    const auto* replayedTrack = replayed.state.findTrack(recorded.trackId);
    REQUIRE(replayedTrack != nullptr);
    CHECK(replayedTrack->clips.empty());
    CHECK(replayedTrack->volumeDb == doctest::Approx(0.0));
}

TEST_CASE("Replay refuses an envelope it cannot rebuild")
{
    Harness harness;

    SUBCASE("unknown type")
    {
        CommandEnvelope envelope{};
        envelope.id = CommandId::generate();
        envelope.type = "track.explode";
        envelope.at = Timestamp::now();
        envelope.payload = Value::object({});

        CHECK(harness.bus.executeSerialized(envelope.toValue()).code() == ErrorCode::unknownCommandType);
    }

    SUBCASE("payload that does not match the type")
    {
        CommandEnvelope envelope{};
        envelope.id = CommandId::generate();
        envelope.type = "track.set_volume";
        envelope.at = Timestamp::now();
        envelope.payload = Value::object({{"trackId", Value{harness.trackId.toString()}}});

        CHECK(harness.bus.executeSerialized(envelope.toValue()).code() == ErrorCode::invalidPayload);
    }

    SUBCASE("not an envelope at all")
    {
        CHECK(harness.bus.executeSerialized(Value{42}).code() == ErrorCode::typeMismatch);
    }

    CHECK(harness.bus.undoDepth() == 0);
}
