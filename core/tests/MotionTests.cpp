#include "daw/ui/model/Motion.h"

#include <cmath>

#include <doctest/doctest.h>

using daw::ui::FallingLevel;
using daw::ui::Glide;

TEST_CASE("A glide leaves fast and arrives slowly, and ends where it goes")
{
    const Glide glide{0.0, 100.0, 1000.0, 200.0};

    CHECK(glide.at(900.0) == 0.0);
    CHECK(glide.at(1000.0) == 0.0);
    CHECK(glide.at(1200.0) == 100.0);
    CHECK(glide.at(5000.0) == 100.0);
    CHECK(!glide.done(1199.0));
    CHECK(glide.done(1200.0));

    // Half the time, most of the way: an ease-out, not a line.
    const auto half = glide.at(1100.0);
    CHECK(half == doctest::Approx(87.5));

    // Each image a little further, never back.
    double before = glide.at(1000.0);
    for (double now = 1010.0; now <= 1200.0; now += 10.0)
    {
        CHECK(glide.at(now) >= before);
        before = glide.at(now);
    }
}

TEST_CASE("A glide of no duration is already there")
{
    const Glide glide{3.0, 7.0, 0.0, 0.0};
    CHECK(glide.at(0.0) == 7.0);
    CHECK(glide.done(0.0));
}

TEST_CASE("A level rises at once and falls at its rate")
{
    FallingLevel level;
    CHECK(level.advance(-6.0f, 0.0, 24.0f, 0.0, -100.0f) == -6.0f);

    // Silence read: half a second later, 12 dB down, not at the floor.
    CHECK(level.advance(-100.0f, 500.0, 24.0f, 0.0, -100.0f) == doctest::Approx(-18.0f));

    // A louder reading takes it up at once.
    CHECK(level.advance(-3.0f, 520.0, 24.0f, 0.0, -100.0f) == -3.0f);

    // A reading above the fall stops it there.
    CHECK(level.advance(-4.0f, 1520.0, 24.0f, 0.0, -100.0f) == -4.0f);

    // And it never goes under the floor.
    CHECK(level.advance(-100.0f, 60000.0, 24.0f, 0.0, -100.0f) == -100.0f);
}

TEST_CASE("A peak holds before it falls")
{
    FallingLevel peak;
    static_cast<void>(peak.advance(-2.0f, 0.0, 24.0f, 800.0, -100.0f));

    CHECK(peak.advance(-100.0f, 400.0, 24.0f, 800.0, -100.0f) == -2.0f);
    CHECK(peak.advance(-100.0f, 800.0, 24.0f, 800.0, -100.0f) == -2.0f);

    // Past the hold, only the time past it counts: 200 ms, 4.8 dB.
    CHECK(peak.advance(-100.0f, 1000.0, 24.0f, 800.0, -100.0f) == doctest::Approx(-6.8f));
}

namespace
{

// An engine whose position moves once per audio block of 1024 samples at
// 44.1 kHz, and whose blocks reach the message thread late by a varying few
// milliseconds, as a real device's do. Images every 1/60 s.
struct PlayedSong
{
    static constexpr double blockMs = 1024.0 / 44.1;
    static constexpr double imageMs = 1000.0 / 60.0;

    // The engine's position as the message thread sees it at `nowMs`, for a
    // song that started moving at `startMs`.
    [[nodiscard]] static double engineSeconds(double nowMs, double startMs)
    {
        if (nowMs < startMs)
            return 0.0;
        // Late by 0 to 20 ms, in a pattern that does not line up with images:
        // on this machine, a block came in up to a whole image behind the
        // previous guess (S19).
        const auto lateMs = [](int block) { return static_cast<double>((block * 13) % 21); };
        int block = static_cast<int>((nowMs - startMs) / blockMs);
        while (block > 0 && startMs + block * blockMs + lateMs(block) > nowMs)
            --block;
        return block * blockMs / 1000.0;
    }
};

} // namespace

TEST_CASE("The drawn playhead moves at every image, and stays with the engine")
{
    daw::ui::DrawnPlayhead playhead;
    const auto startMs = 1000.0;

    double before = playhead.advance(0.0, 0.0);
    int still = 0;
    for (int image = 1; image < 600; ++image)
    {
        const auto now = image * PlayedSong::imageMs;
        const auto drawn = playhead.advance(PlayedSong::engineSeconds(now, startMs), now);

        if (now > startMs + 2 * PlayedSong::blockMs)
        {
            // Some way forward at every image: a quarter of an image's time at
            // least, never a standstill.
            if (drawn - before < 0.25 * PlayedSong::imageMs / 1000.0)
                ++still;

            // And never further than a block from where the song really is.
            CHECK(std::abs(drawn - (now - startMs) / 1000.0) < daw::ui::DrawnPlayhead::blockSeconds);
        }
        CHECK(drawn >= before);
        before = drawn;
    }
    CHECK(still == 0);
}

TEST_CASE("The drawn playhead waits for the engine to start, and for a device that stalls")
{
    daw::ui::DrawnPlayhead playhead;
    CHECK(playhead.advance(2.0, 0.0) == 2.0);

    // The device starts late: the playhead does not leave without it.
    CHECK(playhead.advance(2.0, 100.0) == 2.0);
    CHECK(playhead.advance(2.0, 200.0) == 2.0);

    // It moves: from there.
    CHECK(playhead.advance(2.02, 210.0) == doctest::Approx(2.02));
    CHECK(playhead.advance(2.02, 226.0) > 2.02);

    // The device stalls: the playhead goes no further than a block past it.
    double drawn = 0.0;
    for (double now = 226.0; now < 1000.0; now += 16.0)
        drawn = playhead.advance(2.02, now);
    CHECK(drawn <= 2.02 + daw::ui::DrawnPlayhead::blockSeconds + 1e-9);
}

TEST_CASE("The drawn playhead follows a loop and a jump, and starts again after a stop")
{
    daw::ui::DrawnPlayhead playhead;
    static_cast<void>(playhead.advance(0.0, 0.0));
    static_cast<void>(playhead.advance(0.01, 10.0));
    for (double now = 26.0; now < 4000.0; now += 16.0)
        static_cast<void>(playhead.advance(now / 1000.0, now));

    // The loop goes back to the start: so does the playhead.
    CHECK(playhead.advance(0.0, 4000.0) == 0.0);

    // A click on the ruler, far ahead.
    CHECK(playhead.advance(30.0, 4016.0) == doctest::Approx(30.0));

    playhead.stop();
    CHECK(playhead.advance(5.0, 5000.0) == 5.0);
}
