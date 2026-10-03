#include "daw/ui/model/WindowSnap.h"

#include <vector>

#include <doctest/doctest.h>

using namespace daw::ui::snap;

namespace
{

const Box desktop{0, 0, 1000, 600};
constexpr int reach = 8;

} // namespace

TEST_CASE("a dragged window lands on the edge of the desktop or of a window it faces")
{
    // Near the left edge of the desktop: on it.
    CHECK(moved({6, 100, 300, 200}, desktop, {}, reach) == Box{0, 100, 300, 200});
    // Its right edge near the desktop's right edge.
    CHECK(moved({695, 100, 300, 200}, desktop, {}, reach) == Box{700, 100, 300, 200});
    // Too far: left where the hand put it.
    CHECK(moved({20, 100, 300, 200}, desktop, {}, reach) == Box{20, 100, 300, 200});

    // Beside another window: its left edge on the other's right edge, and its
    // top on the other's top, in one drag.
    const std::vector<Box> others{{0, 0, 400, 300}};
    CHECK(moved({405, 4, 300, 200}, desktop, others, reach) == Box{400, 0, 300, 200});

    // A window that does not face it, far below: its edges do not count.
    const std::vector<Box> below{{0, 500, 400, 100}};
    CHECK(moved({405, 100, 300, 200}, desktop, below, reach) == Box{405, 100, 300, 200});
}

TEST_CASE("a resized window lands the edge it moves, and only that one")
{
    const std::vector<Box> others{{500, 0, 500, 600}};
    // The right edge dragged to 495: on the other window's left edge.
    CHECK(resized({0, 0, 495, 600}, right, desktop, others, reach) == Box{0, 0, 500, 600});
    // The bottom edge near the desktop's.
    CHECK(resized({0, 0, 495, 594}, bottom, desktop, others, reach) == Box{0, 0, 495, 600});
    // The left edge was not moved: it stays at 3 although the desktop is near.
    CHECK(resized({3, 0, 492, 600}, right, desktop, others, reach) == Box{3, 0, 497, 600});
}

TEST_CASE("two windows that touch are resized together, never under their least size")
{
    const Box rack{0, 0, 400, 600};
    const std::vector<Box> others{{400, 0, 600, 300}, {400, 300, 600, 300}, {0, 600, 100, 100}};
    const auto links = linked(rack, right, others);
    REQUIRE(links.size() == 2); // the two on its right; not the one under it
    CHECK(links[0].other == 0);
    CHECK(links[0].edge == left);
    CHECK(links[1].other == 1);

    // Its right edge 100 px to the right: both neighbours lose 100 px.
    const auto wider = follow(rack, {0, 0, 500, 600}, links, others, 200, 100);
    CHECK(wider.window == Box{0, 0, 500, 600});
    CHECK(wider.others[0] == Box{500, 0, 500, 300});
    CHECK(wider.others[1] == Box{500, 300, 500, 300});
    CHECK(wider.others[2] == others[2]);

    // 500 px to the right would leave them 100 px: the edge stops at 200.
    const auto stopped = follow(rack, {0, 0, 900, 600}, links, others, 200, 100);
    CHECK(stopped.window == Box{0, 0, 800, 600});
    CHECK(stopped.others[0] == Box{800, 0, 200, 300});

    // Windows that do not touch are not held.
    CHECK(linked(rack, right, {{410, 0, 500, 600}}).empty());

    // The edge between two stacked windows, from below.
    const Box history{0, 300, 400, 300};
    const std::vector<Box> above{{0, 0, 400, 300}};
    const auto up = linked(history, top, above);
    REQUIRE(up.size() == 1);
    CHECK(up[0].edge == bottom);
    const auto taller = follow(history, {0, 250, 400, 350}, up, above, 200, 100);
    CHECK(taller.others[0] == Box{0, 0, 400, 250});
}
