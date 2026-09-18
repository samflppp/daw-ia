#include "TestSupport.h"

#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
using daw::testing::RecordingObserver;

TEST_CASE("Undoing a whole sequence returns to the exact starting state")
{
    Harness harness;
    RecordingObserver observer;
    harness.bus.addObserver(observer);

    const auto clipId = ClipId::generate();
    const auto noteId = NoteId::generate();

    // A snapshot after each step, so undo is compared against the real state and
    // not only against the first and the last.
    std::vector<Value> snapshots;
    snapshots.push_back(harness.state.toValue());

    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    snapshots.push_back(harness.state.toValue());

    REQUIRE(harness.bus.execute(Harness::addNote(clipId, noteId, 64)).ok());
    snapshots.push_back(harness.state.toValue());

    REQUIRE(harness.bus.execute(harness.setVolume(-6.0)).ok());
    snapshots.push_back(harness.state.toValue());

    CHECK(harness.bus.undoDepth() == 3);

    for (std::size_t step = 3; step > 0; --step)
    {
        REQUIRE(harness.bus.undo().ok());
        CHECK(harness.state.toValue() == snapshots[step - 1]);
    }

    CHECK_FALSE(harness.bus.canUndo());
    CHECK(harness.bus.redoDepth() == 3);
    CHECK(observer.undone.size() == 3);

    for (std::size_t step = 1; step <= 3; ++step)
    {
        REQUIRE(harness.bus.redo().ok());
        CHECK(harness.state.toValue() == snapshots[step]);
    }

    CHECK_FALSE(harness.bus.canRedo());
    CHECK(harness.bus.undoDepth() == 3);
    CHECK(observer.redone.size() == 3);
}

TEST_CASE("Undo and redo can be interleaved without drifting")
{
    Harness harness;
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    const auto afterClip = harness.state.toValue();

    REQUIRE(harness.bus.execute(Harness::addNote(clipId, NoteId::generate())).ok());
    const auto afterNote = harness.state.toValue();

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == afterClip);

    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.toValue() == afterNote);

    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.toValue() == afterClip);
}

TEST_CASE("Undoing a volume change restores the value it overwrote")
{
    Harness harness;

    REQUIRE(harness.bus.execute(harness.setVolume(-4.0)).ok());
    REQUIRE(harness.bus.execute(harness.setVolume(-12.0)).ok());

    CHECK(harness.state.trackVolume(harness.trackId).value() == doctest::Approx(-12.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.trackVolume(harness.trackId).value() == doctest::Approx(-4.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.trackVolume(harness.trackId).value() == doctest::Approx(0.0));
}

TEST_CASE("A new command after an undo drops the abandoned branch")
{
    Harness harness;
    const auto firstClip = ClipId::generate();
    const auto secondClip = ClipId::generate();

    REQUIRE(harness.bus.execute(harness.createClip(firstClip)).ok());
    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.bus.execute(harness.createClip(secondClip)).ok());

    CHECK(harness.bus.redoDepth() == 0);
    CHECK(harness.bus.redo().code() == ErrorCode::nothingToRedo);
    CHECK(harness.state.findClip(firstClip) == nullptr);
    CHECK(harness.state.findClip(secondClip) != nullptr);
}

TEST_CASE("Undo reports the entry it moved, with the new depths")
{
    Harness harness;
    const auto executed = harness.bus.execute(harness.createClip(ClipId::generate()));
    REQUIRE(executed.ok());

    const auto undone = harness.bus.undo();
    REQUIRE(undone.ok());

    CHECK(undone.value().id == executed.value().id);
    CHECK(undone.value().type == "clip.create_midi");
    CHECK(undone.value().undoDepth == 0);
    CHECK(undone.value().redoDepth == 1);
}
