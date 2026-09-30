#include "daw/ui/model/Motion.h"

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
