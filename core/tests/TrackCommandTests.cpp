#include "TestSupport.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/serialization/Json.h"

#include <memory>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

TEST_CASE("a track is created by a command, so a journal can bring it back")
{
    Harness harness;
    const auto trackId = TrackId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(trackId, "Basse", -3.0)).ok());

    const auto* track = harness.state.findTrack(trackId);
    REQUIRE(track != nullptr);
    CHECK(track->name == "Basse");
    CHECK(track->volumeDb == doctest::Approx(-3.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findTrack(trackId) == nullptr);

    REQUIRE(harness.bus.redo().ok());
    REQUIRE(harness.state.findTrack(trackId) != nullptr);
}

TEST_CASE("the same track identifier cannot be added twice")
{
    Harness harness;
    const auto trackId = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(trackId, "Basse")).ok());

    auto again = harness.bus.execute(std::make_unique<AddTrack>(trackId, "Basse"));
    REQUIRE(!again.ok());
    CHECK(again.error().code == ErrorCode::conflict);
    CHECK(harness.bus.undoDepth() == 1);
}

TEST_CASE("undoing a removal puts the track back with its content and its place")
{
    Harness harness;
    const auto first = TrackId::generate();
    const auto second = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(first, "Batterie")).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(second, "Basse")).ok());

    // Content the command itself knows nothing about.
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreateMidiClip>(first, clipId, 0.0, 4.0)).ok());
    REQUIRE(harness.bus.execute(Harness::addNote(clipId, NoteId::generate(), 48)).ok());

    const auto before = json::write(harness.state.toValue());

    REQUIRE(harness.bus.execute(std::make_unique<RemoveTrack>(first)).ok());
    CHECK(harness.state.findTrack(first) == nullptr);
    CHECK(harness.state.findClip(clipId) == nullptr);

    REQUIRE(harness.bus.undo().ok());
    CHECK(json::write(harness.state.toValue()) == before);

    auto index = harness.state.trackIndex(first);
    REQUIRE(index.ok());
    CHECK(index.value() == 1); // behind the track the harness starts with
}

TEST_CASE("removing a track that is not there is an error, not a silent no-op")
{
    Harness harness;
    auto removed = harness.bus.execute(std::make_unique<RemoveTrack>(TrackId::generate()));
    REQUIRE(!removed.ok());
    CHECK(removed.error().code == ErrorCode::notFound);
    CHECK(harness.bus.undoDepth() == 0);
}

TEST_CASE("a journal of track commands replays into the same project")
{
    Harness source;
    const auto trackId = TrackId::generate();
    REQUIRE(source.bus.execute(std::make_unique<AddTrack>(trackId, "Voix", -1.5)).ok());
    const auto clipId = ClipId::generate();
    REQUIRE(source.bus.execute(std::make_unique<CreateMidiClip>(trackId, clipId, 0.0, 4.0)).ok());
    REQUIRE(source.bus.execute(Harness::addNote(clipId, NoteId::generate(), 72)).ok());

    ProjectState replayedState;
    const auto registry = CommandRegistry::withBuiltinCommands();
    CommandBus replayed{replayedState, registry};

    // Nothing is seeded by hand: the track exists because a command created it.
    for (const auto& envelope : source.bus.journal())
        REQUIRE(replayed.executeSerialized(envelope).ok());

    REQUIRE(replayedState.findTrack(trackId) != nullptr);
    REQUIRE(replayed.canUndo());
    REQUIRE(replayed.undo().ok());
    CHECK(replayedState.findClip(clipId)->notes.empty());
}
