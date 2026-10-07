#include "daw/domain/voice/PushToTalk.h"
#include "daw/domain/voice/Removals.h"

#include <doctest/doctest.h>

using daw::domain::voice::PushToTalk;

// The push-to-talk's gesture (S25): every end but a release in time ends
// without a phrase.

namespace
{

constexpr int rightCtrl = PushToTalk::keyScanCode;

} // namespace

TEST_CASE("Held then released in time: the microphone opens, then closes for the transcriber")
{
    PushToTalk talk;
    const auto pressed = talk.key(rightCtrl, true, true, 10.0);
    CHECK(pressed.openMicrophone);
    CHECK(talk.state() == PushToTalk::State::listening);

    // Windows repeats a held key: nothing more.
    CHECK_FALSE(talk.key(rightCtrl, true, true, 10.4).openMicrophone);

    const auto released = talk.key(rightCtrl, true, false, 12.0);
    CHECK(released.closeMicrophone);
    CHECK(released.ended == PushToTalk::Ended::released);
    CHECK(talk.state() == PushToTalk::State::transcribing);

    // Pressed again before the phrase is back: not a second phrase.
    CHECK_FALSE(talk.key(rightCtrl, true, true, 12.2).openMicrophone);
    talk.transcribed();
    CHECK(talk.state() == PushToTalk::State::idle);
}

TEST_CASE("The left Ctrl, and AltGr which Windows sends as one, are not the key")
{
    PushToTalk talk;
    CHECK_FALSE(talk.key(rightCtrl, false, true, 1.0).openMicrophone);
    CHECK(talk.state() == PushToTalk::State::idle);
}

TEST_CASE("Released too early, held too long, another key, the focus lost: no phrase")
{
    SUBCASE("too short")
    {
        PushToTalk talk;
        static_cast<void>(talk.key(rightCtrl, true, true, 1.0));
        const auto step = talk.key(rightCtrl, true, false, 1.4);
        CHECK(step.closeMicrophone);
        CHECK(step.ended == PushToTalk::Ended::tooShort);
        CHECK(talk.state() == PushToTalk::State::idle);
    }
    SUBCASE("too long, by the clock")
    {
        PushToTalk talk;
        static_cast<void>(talk.key(rightCtrl, true, true, 1.0));
        CHECK(talk.tick(15.9).ended == PushToTalk::Ended::none);
        const auto step = talk.tick(16.1);
        CHECK(step.ended == PushToTalk::Ended::tooLong);
        CHECK(talk.state() == PushToTalk::State::idle);
        // Its release, later, is nothing.
        CHECK(talk.key(rightCtrl, true, false, 17.0).ended == PushToTalk::Ended::none);
    }
    SUBCASE("another key: a shortcut, not a phrase")
    {
        PushToTalk talk;
        static_cast<void>(talk.key(rightCtrl, true, true, 1.0));
        const auto step = talk.key(0x2C, false, true, 1.6); // Z, Ctrl+Z
        CHECK(step.ended == PushToTalk::Ended::otherKey);
        CHECK(talk.state() == PushToTalk::State::idle);
    }
    SUBCASE("the window loses the focus")
    {
        PushToTalk talk;
        static_cast<void>(talk.button(true, 1.0));
        CHECK(talk.focusLost(2.0).ended == PushToTalk::Ended::focusLost);
        CHECK(talk.state() == PushToTalk::State::idle);
    }
}

TEST_CASE("The button on the screen does what the key does")
{
    PushToTalk talk;
    CHECK(talk.button(true, 3.0).openMicrophone);
    const auto step = talk.button(false, 5.0);
    CHECK(step.ended == PushToTalk::Ended::released);
}

TEST_CASE("A phrase spoken that removes is named before it is written, one that only edits is not")
{
    using daw::domain::voice::removals;
    CHECK(removals({"track.set_volume", "plugin.insert"}).empty());
    CHECK(removals({"note.remove", "note.remove", "note.remove"}).empty()); // three notes: an edit

    const auto track = removals({"track.remove"});
    REQUIRE(track.size() == 1);
    CHECK(track.front() == "retire une piste");

    // Emptying a pattern is many notes removed.
    const auto emptied =
        removals({"note.remove", "note.remove", "note.remove", "note.remove", "note.remove"});
    REQUIRE(emptied.size() == 1);
    CHECK(emptied.front() == "retire 5 notes");

    const auto both = removals({"plugin.remove", "track.remove", "track.remove"});
    REQUIRE(both.size() == 2);
    CHECK(both[0] == "retire un plugin");
    CHECK(both[1] == "retire 2 pistes");
}
