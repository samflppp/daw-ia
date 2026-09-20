#include "TestSupport.h"
#include "daw/domain/commands/NoteCommands.h"

#include <memory>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

// A clip with three notes, so that the middle one can be removed: removing the
// last one would pass whatever insertNote does with its index.
struct Clip3
{
    Harness harness;
    ClipId clipId{ClipId::generate()};
    NoteId first{NoteId::generate()};
    NoteId middle{NoteId::generate()};
    NoteId last{NoteId::generate()};

    Clip3()
    {
        REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clipId, first, 60)).ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clipId, middle, 64)).ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clipId, last, 67)).ok());
    }

    [[nodiscard]] const Note* note(NoteId id) const
    {
        const auto* clip = harness.state.findClip(clipId);
        for (const auto& note : clip->notes)
        {
            if (note.id == id)
                return &note;
        }
        return nullptr;
    }
};

} // namespace

TEST_CASE("a removed note comes back where it was, not at the end")
{
    Clip3 fixture;
    const auto before = fixture.harness.state.toValue();

    REQUIRE(fixture.harness.bus.execute(std::make_unique<RemoveNote>(fixture.clipId, fixture.middle)).ok());
    CHECK(fixture.harness.state.findClip(fixture.clipId)->notes.size() == 2);
    CHECK(fixture.note(fixture.middle) == nullptr);

    REQUIRE(fixture.harness.bus.undo().ok());

    // Not "the three notes are back": the exact same project. Order is part of
    // the serialized form, so a note put back at the end would pass a size
    // check and fail this one.
    CHECK(fixture.harness.state.toValue() == before);
}

TEST_CASE("removing a note that is not there changes nothing and creates no entry")
{
    Clip3 fixture;
    const auto depth = fixture.harness.bus.undoDepth();

    auto removed =
        fixture.harness.bus.execute(std::make_unique<RemoveNote>(fixture.clipId, NoteId::generate()));
    REQUIRE(!removed.ok());
    CHECK(removed.error().code == ErrorCode::notFound);
    CHECK(fixture.harness.bus.undoDepth() == depth);
    CHECK(fixture.harness.state.findClip(fixture.clipId)->notes.size() == 3);
}

TEST_CASE("a note moves in time and in pitch, and its length does not follow")
{
    Clip3 fixture;
    const auto length = fixture.note(fixture.middle)->lengthBeats;

    REQUIRE(fixture.harness.bus.execute(std::make_unique<MoveNote>(fixture.clipId, fixture.middle, 72, 2.5))
                .ok());

    CHECK(fixture.note(fixture.middle)->pitch == 72);
    CHECK(fixture.note(fixture.middle)->startBeats == doctest::Approx(2.5));
    CHECK(fixture.note(fixture.middle)->lengthBeats == doctest::Approx(length));

    REQUIRE(fixture.harness.bus.undo().ok());
    CHECK(fixture.note(fixture.middle)->pitch == 64);
    CHECK(fixture.note(fixture.middle)->startBeats == doctest::Approx(0.0));
}

TEST_CASE("a move out of range is refused, and the note does not budge")
{
    Clip3 fixture;
    const auto depth = fixture.harness.bus.undoDepth();

    auto moved =
        fixture.harness.bus.execute(std::make_unique<MoveNote>(fixture.clipId, fixture.middle, 128, 1.0));
    REQUIRE(!moved.ok());
    CHECK(moved.error().code == ErrorCode::invalidArgument);

    CHECK(fixture.note(fixture.middle)->pitch == 64);
    CHECK(fixture.note(fixture.middle)->startBeats == doctest::Approx(0.0));
    CHECK(fixture.harness.bus.undoDepth() == depth);

    auto backwards =
        fixture.harness.bus.execute(std::make_unique<MoveNote>(fixture.clipId, fixture.middle, 64, -1.0));
    REQUIRE(!backwards.ok());
    CHECK(fixture.note(fixture.middle)->startBeats == doctest::Approx(0.0));
}

TEST_CASE("a drag is one history entry, and undoing it gives back the note as it was")
{
    Clip3 fixture;
    const auto depth = fixture.harness.bus.undoDepth();

    const auto gesture = fixture.harness.bus.beginGesture("deplacer une note");
    ExecuteOptions options{};
    options.gesture = gesture;

    // Sixty frames of a drag, exactly what a mouse produces.
    for (int frame = 1; frame <= 60; ++frame)
    {
        const auto position = static_cast<double>(frame) / 60.0;
        REQUIRE(fixture.harness.bus
                    .execute(std::make_unique<MoveNote>(fixture.clipId, fixture.middle, 64 + frame / 20, position),
                             options)
                    .ok());
    }
    REQUIRE(fixture.harness.bus.endGesture(gesture).ok());

    CHECK(fixture.harness.bus.undoDepth() == depth + 1);
    CHECK(fixture.note(fixture.middle)->startBeats == doctest::Approx(1.0));

    REQUIRE(fixture.harness.bus.undo().ok());
    CHECK(fixture.note(fixture.middle)->pitch == 64);
    CHECK(fixture.note(fixture.middle)->startBeats == doctest::Approx(0.0));
}

TEST_CASE("two notes moved inside one gesture stay two entries")
{
    Clip3 fixture;
    const auto depth = fixture.harness.bus.undoDepth();

    const auto gesture = fixture.harness.bus.beginGesture("deplacer");
    ExecuteOptions options{};
    options.gesture = gesture;

    REQUIRE(
        fixture.harness.bus.execute(std::make_unique<MoveNote>(fixture.clipId, fixture.first, 60, 1.0), options)
            .ok());
    REQUIRE(
        fixture.harness.bus.execute(std::make_unique<MoveNote>(fixture.clipId, fixture.last, 67, 3.0), options)
            .ok());
    REQUIRE(fixture.harness.bus.endGesture(gesture).ok());

    CHECK(fixture.harness.bus.undoDepth() == depth + 2);

    // Undoing one must not move the other.
    REQUIRE(fixture.harness.bus.undo().ok());
    CHECK(fixture.note(fixture.last)->startBeats == doctest::Approx(0.0));
    CHECK(fixture.note(fixture.first)->startBeats == doctest::Approx(1.0));
}

TEST_CASE("the journal replays a removal and a drag into the same project")
{
    Clip3 fixture;

    REQUIRE(fixture.harness.bus.execute(std::make_unique<RemoveNote>(fixture.clipId, fixture.first)).ok());

    const auto gesture = fixture.harness.bus.beginGesture("deplacer");
    ExecuteOptions options{};
    options.gesture = gesture;
    for (int frame = 1; frame <= 10; ++frame)
    {
        REQUIRE(fixture.harness.bus
                    .execute(std::make_unique<MoveNote>(fixture.clipId, fixture.last, 67, frame * 0.25),
                             options)
                    .ok());
    }
    REQUIRE(fixture.harness.bus.endGesture(gesture).ok());

    ProjectState replayedState;
    const auto registry = CommandRegistry::withBuiltinCommands();
    CommandBus replayed{replayedState, registry};

    // The harness seeds its track by hand, so the replay is given the same
    // starting point and nothing else.
    Track seed{};
    seed.id = fixture.harness.trackId;
    seed.name = "Piste 1";
    REQUIRE(replayedState.addTrack(seed).ok());

    for (const auto& envelope : fixture.harness.bus.journal())
        REQUIRE(replayed.executeSerialized(envelope).ok());

    CHECK(replayedState == fixture.harness.state);
}

TEST_CASE("the note commands are registered under their own names")
{
    const auto registry = CommandRegistry::withBuiltinCommands();
    CHECK(registry.contains("note.add"));
    CHECK(registry.contains("note.remove"));
    CHECK(registry.contains("note.move"));
}
