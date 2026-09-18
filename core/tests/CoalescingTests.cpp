#include "TestSupport.h"
#include "daw/domain/command/CommandEnvelope.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
using daw::testing::RecordingObserver;

TEST_CASE("Sixty fader frames make one history entry")
{
    Harness harness;
    RecordingObserver observer;
    harness.bus.addObserver(observer);

    const auto gesture = harness.bus.beginGesture("fader volume piste 1");

    for (int frame = 0; frame < 60; ++frame)
    {
        const auto volumeDb = -0.1 * static_cast<double>(frame);
        REQUIRE(harness.bus.execute(harness.setVolume(volumeDb), ExecuteOptions{gesture}).ok());
    }

    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == 1);
    CHECK(observer.executed.size() == 1);   // the first frame opened the entry
    CHECK(observer.coalesced.size() == 59); // the other 59 merged into it
    CHECK(harness.bus.journal().size() == 1);
    CHECK(harness.state.trackVolume(harness.trackId).value() == doctest::Approx(-5.9));

    // One undo, and the whole drag is gone.
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.trackVolume(harness.trackId).value() == doctest::Approx(0.0));
    CHECK_FALSE(harness.bus.canUndo());
}

TEST_CASE("The journal keeps the last value of the gesture, not the first")
{
    Harness harness;
    const auto gesture = harness.bus.beginGesture("fader");

    REQUIRE(harness.bus.execute(harness.setVolume(-2.0), ExecuteOptions{gesture}).ok());
    REQUIRE(harness.bus.execute(harness.setVolume(-9.0), ExecuteOptions{gesture}).ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    const auto journal = harness.bus.journal();
    REQUIRE(journal.size() == 1);

    const auto envelope = CommandEnvelope::fromValue(journal.front());
    REQUIRE(envelope.ok());
    CHECK(envelope.value().payload.doubleAt("volumeDb").value() == doctest::Approx(-9.0));
    REQUIRE(envelope.value().gesture.has_value());
    CHECK(*envelope.value().gesture == gesture);
}

TEST_CASE("A receipt of a merged command names the entry, not the command")
{
    Harness harness;
    const auto gesture = harness.bus.beginGesture("fader");

    const auto first = harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{gesture});
    const auto second = harness.bus.execute(harness.setVolume(-2.0), ExecuteOptions{gesture});
    REQUIRE(first.ok());
    REQUIRE(second.ok());

    CHECK_FALSE(first.value().coalesced);
    CHECK(second.value().coalesced);
    CHECK(second.value().id == first.value().id);
    CHECK(second.value().at == first.value().at);
    CHECK(second.value().undoDepth == 1);
}

TEST_CASE("Nothing merges by accident")
{
    Harness harness;

    SUBCASE("no gesture, no merging")
    {
        REQUIRE(harness.bus.execute(harness.setVolume(-1.0)).ok());
        REQUIRE(harness.bus.execute(harness.setVolume(-2.0)).ok());
        CHECK(harness.bus.undoDepth() == 2);
    }

    SUBCASE("a closed gesture does not reopen")
    {
        const auto first = harness.bus.beginGesture("fader");
        REQUIRE(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{first}).ok());
        REQUIRE(harness.bus.endGesture(first).ok());

        const auto second = harness.bus.beginGesture("fader");
        REQUIRE(harness.bus.execute(harness.setVolume(-2.0), ExecuteOptions{second}).ok());
        REQUIRE(harness.bus.endGesture(second).ok());

        CHECK(harness.bus.undoDepth() == 2);
    }

    SUBCASE("another track is another entry")
    {
        const auto otherTrack = TrackId::generate();
        Track track{};
        track.id = otherTrack;
        track.name = "Piste 2";
        REQUIRE(harness.state.addTrack(track).ok());

        const auto gesture = harness.bus.beginGesture("fader");
        REQUIRE(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{gesture}).ok());
        REQUIRE(
            harness.bus.execute(std::make_unique<SetTrackVolume>(otherTrack, -1.0), ExecuteOptions{gesture})
                .ok());
        REQUIRE(harness.bus.endGesture(gesture).ok());

        CHECK(harness.bus.undoDepth() == 2);
    }

    SUBCASE("a structural command never merges")
    {
        const auto gesture = harness.bus.beginGesture("dessin de notes");
        const auto clipId = ClipId::generate();
        REQUIRE(harness.bus.execute(harness.createClip(clipId), ExecuteOptions{gesture}).ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clipId, NoteId::generate(), 60), ExecuteOptions{gesture})
                    .ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clipId, NoteId::generate(), 64), ExecuteOptions{gesture})
                    .ok());
        REQUIRE(harness.bus.endGesture(gesture).ok());

        CHECK(harness.bus.undoDepth() == 3);
    }

    SUBCASE("a volume change after another command does not reach back")
    {
        const auto gesture = harness.bus.beginGesture("mixte");
        REQUIRE(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{gesture}).ok());
        REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate()), ExecuteOptions{gesture}).ok());
        REQUIRE(harness.bus.execute(harness.setVolume(-2.0), ExecuteOptions{gesture}).ok());
        REQUIRE(harness.bus.endGesture(gesture).ok());

        CHECK(harness.bus.undoDepth() == 3);
    }
}

TEST_CASE("A gesture identifier that is not the open one is refused")
{
    Harness harness;
    const auto stale = GestureId::generate();

    CHECK(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{stale}).code() ==
          ErrorCode::gestureClosed);
    CHECK(harness.bus.endGesture(stale).code() == ErrorCode::gestureClosed);
    CHECK(harness.bus.undoDepth() == 0);

    const auto open = harness.bus.beginGesture("fader");
    CHECK(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{stale}).code() ==
          ErrorCode::gestureClosed);
    CHECK(harness.bus.endGesture(open).ok());
}

TEST_CASE("Opening a second gesture closes the first")
{
    Harness harness;

    const auto first = harness.bus.beginGesture("fader");
    REQUIRE(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{first}).ok());

    const auto second = harness.bus.beginGesture("autre fader");
    CHECK(harness.bus.openGesture().has_value());
    CHECK(*harness.bus.openGesture() == second);
    CHECK(harness.bus.openGestureLabel() == "autre fader");

    CHECK(harness.bus.execute(harness.setVolume(-2.0), ExecuteOptions{first}).code() ==
          ErrorCode::gestureClosed);
    REQUIRE(harness.bus.execute(harness.setVolume(-3.0), ExecuteOptions{second}).ok());

    CHECK(harness.bus.undoDepth() == 2);
}

TEST_CASE("An undo closes the open gesture")
{
    Harness harness;
    const auto gesture = harness.bus.beginGesture("fader");

    REQUIRE(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{gesture}).ok());
    REQUIRE(harness.bus.undo().ok());

    CHECK_FALSE(harness.bus.openGesture().has_value());
    CHECK(harness.bus.execute(harness.setVolume(-2.0), ExecuteOptions{gesture}).code() ==
          ErrorCode::gestureClosed);
}
