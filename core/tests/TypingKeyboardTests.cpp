#include "daw/domain/live/Router.h"
#include "daw/domain/live/TypingKeyboard.h"

#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain::live;

// The computer's keyboard as a piano (S23): by the place of a key, never its
// character. What a key plays is read where it goes: in the queue of the
// track, the way the engine reads it.

namespace
{

struct Played
{
    std::vector<int> on;
    std::vector<int> off;
    std::vector<int> velocities;
};

Played drain(TrackQueue& queue)
{
    Played played;
    Event event;
    while (queue.pop(event))
    {
        if ((event.bytes[0] & 0xF0) == 0x90)
        {
            played.on.push_back(event.bytes[1]);
            played.velocities.push_back(event.bytes[2]);
        }
        else if ((event.bytes[0] & 0xF0) == 0x80)
        {
            played.off.push_back(event.bytes[1]);
        }
    }
    return played;
}

struct Rig
{
    Rig()
    {
        queue = router.queueFor("A");
        router.setTarget("A", 0.0);
        static_cast<void>(drain(*queue));
        keys.setPlaying(true);
    }

    void tap(int scanCode, bool extended = false)
    {
        static_cast<void>(keys.key(scanCode, extended, true, 0.0));
        static_cast<void>(keys.key(scanCode, extended, false, 0.1));
    }

    Router router;
    TypingKeyboard keys{router};
    TrackQueue* queue{nullptr};
};

constexpr int scanW = 0x2C; // AZERTY W, QWERTY Z: the C of the bottom row
constexpr int scanA = 0x10; // AZERTY A, QWERTY Q: the C an octave up
constexpr int scanF = 0x21; // F: not a note, it frames
constexpr int scanK = 0x25; // K: not a note
constexpr int scanCtrl = 0x1D;
constexpr int scanLeft = TypingKeyboard::leftArrow;
constexpr int scanRight = TypingKeyboard::rightArrow;

} // namespace

TEST_CASE("Typing keyboard: every key of the two rows plays its pitch, by its place")
{
    Rig rig;
    std::set<int> pitches;
    for (const auto& key : TypingKeyboard::table())
    {
        rig.tap(key.scanCode);
        const auto played = drain(*rig.queue);
        REQUIRE(played.on.size() == 1);
        CHECK(played.on[0] == 60 + key.semitone);
        CHECK(played.off == played.on);
        pitches.insert(played.on[0]);
    }
    // From the C of the bottom row to the G of the upper one, every semitone.
    CHECK(pitches.size() == 32);
    CHECK(*pitches.begin() == 60);
    CHECK(*pitches.rbegin() == 91);

    // The layout of FL by place: the bottom row's C is AZERTY W (QWERTY Z), the
    // upper row's C AZERTY A (QWERTY Q), its C# the key of « é » (QWERTY 2).
    CHECK(TypingKeyboard::semitoneOf(scanW) == 0);
    CHECK(TypingKeyboard::semitoneOf(scanA) == 12);
    CHECK(TypingKeyboard::semitoneOf(0x03) == 13);
    CHECK(TypingKeyboard::semitoneOf(0x1A) == 29); // ^, a dead key on AZERTY, plays like any other
}

TEST_CASE("Typing keyboard: F and K are not notes, nor anything off the two rows")
{
    Rig rig;
    for (const auto scan :
         {scanF, scanK, 0x39 /* space */, 0x0F /* tab */, 0x01 /* escape */, 0x1C /* enter */})
    {
        CHECK_FALSE(rig.keys.key(scan, false, true, 0.0));
        static_cast<void>(rig.keys.key(scan, false, false, 0.1));
    }
    CHECK(drain(*rig.queue).on.empty());
}

TEST_CASE("Typing keyboard: the mode off, a text field, Ctrl held: the keys play nothing")
{
    Rig rig;

    rig.keys.setPlaying(false);
    CHECK_FALSE(rig.keys.key(scanW, false, true, 0.0));
    static_cast<void>(rig.keys.key(scanW, false, false, 0.1));
    CHECK(drain(*rig.queue).on.empty());

    rig.keys.setPlaying(true);
    rig.keys.setTyping(true);
    CHECK_FALSE(rig.keys.key(scanW, false, true, 0.0));
    static_cast<void>(rig.keys.key(scanW, false, false, 0.1));
    CHECK(drain(*rig.queue).on.empty());

    rig.keys.setTyping(false);
    static_cast<void>(rig.keys.key(scanCtrl, false, true, 0.0));
    CHECK_FALSE(rig.keys.key(0x2E /* C: Ctrl+C copies */, false, true, 0.0));
    static_cast<void>(rig.keys.key(0x2E, false, false, 0.1));
    static_cast<void>(rig.keys.key(scanCtrl, false, false, 0.1));
    CHECK(drain(*rig.queue).on.empty());

    // And back to playing once Ctrl is up.
    rig.tap(0x2E);
    CHECK(drain(*rig.queue).on == std::vector<int>{64});
}

TEST_CASE("Typing keyboard: a key held down repeats nothing")
{
    Rig rig;
    for (int repeat = 0; repeat < 10; ++repeat)
        static_cast<void>(rig.keys.key(scanW, false, true, 0.01 * repeat));
    static_cast<void>(rig.keys.key(scanW, false, false, 0.2));
    const auto played = drain(*rig.queue);
    CHECK(played.on == std::vector<int>{60});
    CHECK(played.off == std::vector<int>{60});
}

TEST_CASE("Typing keyboard: the arrows move the octave, and a held key ends the note it began")
{
    Rig rig;
    rig.tap(scanRight, true);
    CHECK(rig.keys.octave() == TypingKeyboard::defaultOctave + 1);
    rig.tap(scanW);
    CHECK(drain(*rig.queue).on == std::vector<int>{72});

    static_cast<void>(rig.keys.key(scanW, false, true, 0.0));
    rig.tap(scanLeft, true);
    rig.tap(scanLeft, true);
    static_cast<void>(rig.keys.key(scanW, false, false, 0.5));
    const auto played = drain(*rig.queue);
    CHECK(played.on == std::vector<int>{72});
    CHECK(played.off == std::vector<int>{72}); // not 48, the octave it would play now
    CHECK(rig.keys.octave() == TypingKeyboard::defaultOctave - 1);

    // Bounded: the top key of the upper row is 127 at the highest octave.
    for (int step = 0; step < 20; ++step)
        rig.tap(scanRight, true);
    CHECK(rig.keys.octave() == TypingKeyboard::highestOctave);
    rig.tap(0x1B); // the last key of the upper row
    CHECK(drain(*rig.queue).on == std::vector<int>{127});

    // Off the mode, the arrows are not the octave's.
    rig.keys.setPlaying(false);
    CHECK_FALSE(rig.keys.key(scanLeft, true, true, 0.0));
    static_cast<void>(rig.keys.key(scanLeft, true, false, 0.0));
    CHECK(rig.keys.octave() == TypingKeyboard::highestOctave);
}

TEST_CASE("Typing keyboard: the fixed velocity is the one set")
{
    Rig rig;
    rig.keys.setVelocity(42);
    rig.tap(scanW);
    CHECK(drain(*rig.queue).velocities == std::vector<int>{42});
    rig.keys.setVelocity(500);
    CHECK(rig.keys.velocity() == 127);
}

TEST_CASE("Typing keyboard: the mode turned off, a text field, the window behind: what is held is released")
{
    for (int which = 0; which < 3; ++which)
    {
        Rig rig;
        static_cast<void>(rig.keys.key(scanW, false, true, 0.0));
        static_cast<void>(rig.keys.key(scanA, false, true, 0.0));
        static_cast<void>(drain(*rig.queue));
        REQUIRE(rig.router.held() == 2);

        if (which == 0)
            rig.keys.setPlaying(false);
        else if (which == 1)
            rig.keys.setTyping(true);
        else
            rig.keys.setForeground(false);

        const auto played = drain(*rig.queue);
        CHECK(std::set<int>(played.off.begin(), played.off.end()) == std::set<int>{60, 72});
        CHECK(rig.router.held() == 0);

        // The key-ups that follow, wherever they go, send nothing more.
        static_cast<void>(rig.keys.key(scanW, false, false, 0.5));
        static_cast<void>(rig.keys.key(scanA, false, false, 0.5));
        CHECK(drain(*rig.queue).off.empty());
    }
}
