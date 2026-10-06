#include "daw/domain/live/Router.h"
#include "daw/domain/live/Timeline.h"

#include <atomic>
#include <cmath>
#include <set>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain::live;

// The live path (S23) is proven by the sound it makes, in the engine's suite.
// What is proven here is where each message goes — which track, which
// release — and when, with the engine out of the way.

namespace
{

std::vector<Event> drain(TrackQueue& queue)
{
    std::vector<Event> out;
    Event event;
    while (queue.pop(event))
        out.push_back(event);
    return out;
}

bool isNoteOn(const Event& event, int note)
{
    return (event.bytes[0] & 0xF0) == 0x90 && event.bytes[1] == note && event.bytes[2] > 0;
}

bool isNoteOff(const Event& event, int note)
{
    return (event.bytes[0] & 0xF0) == 0x80 && event.bytes[1] == note;
}

constexpr int keyboard = Router::computerKeyboard;
constexpr int midi = Router::firstMidiInput;

} // namespace

TEST_CASE("Live: a key plays the chosen track, and nothing when none is chosen")
{
    Router router;
    auto* a = router.queueFor("A");
    REQUIRE(a != nullptr);

    CHECK_FALSE(router.noteOn(keyboard, 1, 60, 100, 0.0));
    CHECK(drain(*a).empty());
    CHECK(router.held() == 0);

    router.setTarget("A");
    CHECK(router.target() == "A");
    CHECK(router.noteOn(keyboard, 1, 60, 100, 1.0));
    const auto events = drain(*a);
    REQUIRE(events.size() == 1);
    CHECK(isNoteOn(events[0], 60));
    CHECK(events[0].bytes[2] == 100);
    CHECK(events[0].seconds == doctest::Approx(1.0));
    CHECK(router.held() == 1);
}

TEST_CASE("Live: a release goes where its note went, whatever is chosen when the key comes up")
{
    Router router;
    auto* a = router.queueFor("A");
    auto* b = router.queueFor("B");
    router.setTarget("A");
    REQUIRE(router.noteOn(midi, 1, 64, 90, 0.0));
    router.setTarget("B");
    REQUIRE(router.noteOff(midi, 1, 64, 0.5));

    const auto onA = drain(*a);
    REQUIRE_FALSE(onA.empty());
    CHECK(isNoteOn(onA.front(), 64));
    CHECK(isNoteOff(onA.back(), 64));
    for (const auto& event : drain(*b))
        CHECK_FALSE(((event.bytes[0] & 0xF0) == 0x80 || (event.bytes[0] & 0xF0) == 0x90));
    CHECK(router.held() == 0);

    // A release with nothing held sends nothing.
    CHECK_FALSE(router.noteOff(midi, 1, 64, 0.6));
    CHECK(drain(*a).empty());
}

TEST_CASE("Live: the pedal comes up where it went down")
{
    Router router;
    auto* a = router.queueFor("A");
    auto* b = router.queueFor("B");
    router.setTarget("A");
    REQUIRE(router.controller(midi, 1, 64, 127, 0.0));
    router.setTarget("B");
    REQUIRE(router.controller(midi, 1, 64, 0, 0.5));

    const auto onA = drain(*a);
    REQUIRE(onA.size() >= 2);
    CHECK(onA.back().bytes[1] == 64);
    CHECK(onA.back().bytes[2] == 0);
    for (const auto& event : drain(*b))
        CHECK(event.bytes[1] != 64);
}

TEST_CASE("Live: an unplugged keyboard, a lost focus, a lost card each end in releases")
{
    Router router;
    auto* a = router.queueFor("A");
    router.setTarget("A");
    REQUIRE(router.noteOn(midi, 1, 60, 100, 0.0));
    REQUIRE(router.noteOn(midi, 1, 64, 100, 0.0));
    REQUIRE(router.noteOn(keyboard, 1, 67, 100, 0.0));
    REQUIRE(router.controller(midi, 1, 64, 127, 0.0));
    static_cast<void>(drain(*a));

    // The MIDI keyboard unplugged: its notes and its pedal, not the others.
    CHECK(router.release(midi, 1.0) == 2);
    auto events = drain(*a);
    std::set<int> released;
    bool pedalUp = false;
    for (const auto& event : events)
    {
        if ((event.bytes[0] & 0xF0) == 0x80)
            released.insert(event.bytes[1]);
        if ((event.bytes[0] & 0xF0) == 0xB0 && event.bytes[1] == 64 && event.bytes[2] == 0)
            pedalUp = true;
    }
    CHECK(released == std::set<int>{60, 64});
    CHECK(pedalUp);
    CHECK(router.held() == 1);

    // The card lost: what waits is dropped, then everything is released.
    REQUIRE(router.noteOn(midi, 1, 72, 100, 2.0));
    router.silence(3.0);
    events = drain(*a);
    released.clear();
    for (const auto& event : events)
    {
        CHECK_FALSE(isNoteOn(event, 72)); // the note-on waiting in the queue is gone
        if ((event.bytes[0] & 0xF0) == 0x80)
            released.insert(event.bytes[1]);
    }
    CHECK(released == std::set<int>{67, 72});
    CHECK(router.held() == 0);
}

TEST_CASE("Live: a key pressed twice without a release closes the first note before the second")
{
    Router router;
    auto* a = router.queueFor("A");
    auto* b = router.queueFor("B");
    router.setTarget("A");
    REQUIRE(router.noteOn(keyboard, 1, 60, 100, 0.0));
    router.setTarget("B");
    REQUIRE(router.noteOn(keyboard, 1, 60, 100, 0.1));

    const auto onA = drain(*a);
    REQUIRE_FALSE(onA.empty());
    CHECK(isNoteOff(onA.back(), 60));
    const auto onB = drain(*b);
    REQUIRE(onB.size() == 1);
    CHECK(isNoteOn(onB[0], 60));
    CHECK(router.held() == 1);
}

TEST_CASE("Live: a full queue refuses, and what it refused is not held")
{
    Router router;
    auto* a = router.queueFor("A");
    router.setTarget("A");
    int accepted = 0;
    for (int index = 0; index < 600; ++index)
        accepted += router.controller(midi, 1, 1, index % 128, 0.0) ? 1 : 0;
    CHECK(accepted == 512);
    CHECK(router.refused() == 88);
    CHECK_FALSE(router.noteOn(midi, 1, 60, 100, 0.0));
    CHECK(router.held() == 0);
    CHECK(drain(*a).size() == 512);
}

TEST_CASE("Live: many writers, one reader, nothing lost or doubled")
{
    Queue<1024> queue;
    constexpr int writers = 4;
    constexpr int each = 20000;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int writer = 0; writer < writers; ++writer)
    {
        threads.emplace_back(
            [&queue, &go, writer]
            {
                while (!go.load())
                {
                }
                for (int index = 0; index < each; ++index)
                {
                    Event event;
                    event.bytes = {static_cast<std::uint8_t>(writer), 0, 0};
                    event.seconds = static_cast<double>(index);
                    while (!queue.push(event))
                        std::this_thread::yield();
                }
            });
    }

    std::vector<int> next(writers, 0);
    int received = 0;
    bool ordered = true;
    go.store(true);
    while (received < writers * each)
    {
        Event event;
        if (!queue.pop(event))
            continue;
        const auto writer = event.bytes[0];
        ordered = ordered && static_cast<int>(event.seconds) == next[writer];
        next[writer] = static_cast<int>(event.seconds) + 1;
        ++received;
    }
    for (auto& thread : threads)
        thread.join();

    CHECK(ordered); // each writer's events come out in the order it wrote them
    Event extra;
    CHECK_FALSE(queue.pop(extra));
}

TEST_CASE("Live: a note waits one block, the same for every note, whatever the callback's jitter")
{
    // 256 samples at 48 kHz: a block is 5.333 ms. Callbacks come up to half a
    // millisecond early or late; notes are played at known times.
    constexpr double rate = 48000.0;
    constexpr int samples = 256;
    constexpr double block = samples / rate;
    Timeline timeline;

    const double jitter[] = {0.0, 0.0004, -0.0003, 0.0005, -0.0005, 0.0001, 0.0003, -0.0002};
    std::vector<double> heardAt; // when each note sounds, on the timeline
    std::vector<double> playedAt;
    double nextNote = 0.0123;
    for (int index = 0; index < 400; ++index)
    {
        const auto ideal = index * block;
        const auto start = timeline.begin(ideal + jitter[index % 8], samples, rate);
        // Every note played before this callback and not placed yet.
        while (nextNote < ideal + jitter[index % 8] && nextNote < 1.5)
        {
            const auto offset = timeline.offsetOf(nextNote);
            if (offset >= samples)
                break;
            heardAt.push_back(start + offset / rate);
            playedAt.push_back(nextNote);
            nextNote += 0.0371; // an irregular step, never a multiple of a block
        }
    }

    REQUIRE(playedAt.size() > 30);
    double least = 1.0;
    double most = -1.0;
    for (std::size_t index = 5; index < playedAt.size(); ++index) // past the start
    {
        const auto wait = heardAt[index] - playedAt[index];
        least = std::min(least, wait);
        most = std::max(most, wait);
    }
    // Every note waits one block and its margin (a quarter of a block here),
    // give or take a sample and the timeline's small lag behind the clock:
    // never the block of jitter of the start of the next block, nor the
    // callback's own.
    const auto wait = block + 0.25 * block;
    CHECK(least > wait - 0.0001);
    CHECK(most < wait + 0.0001);
    CHECK(most - least < 0.00005);
}

TEST_CASE("Live: the timeline starts again from the clock when it is far off")
{
    Timeline timeline;
    static_cast<void>(timeline.begin(10.0, 512, 48000.0));
    static_cast<void>(timeline.begin(10.0 + 512 / 48000.0, 512, 48000.0));
    // The device stopped for a second.
    const auto start = timeline.begin(11.5, 512, 48000.0);
    CHECK(start == doctest::Approx(11.5));
    // A note played before the stop plays at once, never in the past.
    CHECK(timeline.offsetOf(10.2) == 0);
}

TEST_CASE("Live: while recording, the notes and the pedal go to the take with their track, nothing else")
{
    Router router;
    static_cast<void>(router.queueFor("A"));
    router.setTarget("A", 0.0);

    REQUIRE(router.noteOn(midi, 1, 60, 100, 0.0));
    Event event;
    CHECK_FALSE(router.popTake(event)); // not recording

    router.setRecording(true);
    REQUIRE(router.noteOff(midi, 1, 60, 0.5));
    REQUIRE(router.controller(midi, 1, 64, 127, 0.6));
    REQUIRE(router.controller(midi, 1, 1, 90, 0.7)); // the modulation is played, not written
    REQUIRE(router.pitchBend(midi, 1, 9000, 0.8));   // the bend too

    REQUIRE(router.popTake(event));
    CHECK(isNoteOff(event, 60));
    CHECK(router.trackOf(event.slot) == "A");
    CHECK(event.seconds == doctest::Approx(0.5));
    REQUIRE(router.popTake(event));
    CHECK(event.bytes[1] == 64);
    CHECK_FALSE(router.popTake(event));
}

TEST_CASE("Live: the song's position is read back as it was published")
{
    Router router;
    Position read;
    CHECK_FALSE(router.position(read)); // nothing yet
    router.publish({12.5, 3.25, true});
    REQUIRE(router.position(read));
    CHECK(read.clock == doctest::Approx(12.5));
    CHECK(read.editSeconds == doctest::Approx(3.25));
    CHECK(read.playing);
}
