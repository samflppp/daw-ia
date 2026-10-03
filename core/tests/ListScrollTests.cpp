#include "daw/ui/model/ListScroll.h"

#include <doctest/doctest.h>

using namespace daw::ui::listScroll;

TEST_CASE("a list scrolls between its first row and its last, by the wheel")
{
    // Twenty rows of 30 px in a view of 200: 400 px to travel.
    constexpr int content = 600;
    constexpr int view = 200;
    CHECK(clamped(-5, content, view) == 0);
    CHECK(clamped(450, content, view) == 400);
    CHECK(clamped(120, content, view) == 120);
    // Shorter than its view: it never moves.
    CHECK(clamped(50, 150, view) == 0);

    // Down by a notch of JUCE's wheel (some -0.25 unit): two rows.
    CHECK(wheeled(0, -0.25, 30, content, view) == 60);
    CHECK(wheeled(60, 0.25, 30, content, view) == 0);
    CHECK(wheeled(390, -1.0, 30, content, view) == 400);
}

TEST_CASE("F brings a row into view by the smallest move")
{
    constexpr int content = 600;
    constexpr int view = 200;
    // Already shown: nothing moves.
    CHECK(framed(100, 120, 150, content, view) == 100);
    // Below the view: its bottom on the bottom of the view.
    CHECK(framed(0, 450, 480, content, view) == 280);
    // Above: its top on the top.
    CHECK(framed(300, 60, 90, content, view) == 60);
}

TEST_CASE("the history, newest on top, follows what is added unless the hand scrolled it")
{
    constexpr int content = 600;
    constexpr int view = 200;
    CHECK(afterAdding(0, 1, 30, content, view) == 0);
    CHECK(afterAdding(90, 2, 30, content, view) == 150);
    CHECK(afterAdding(390, 2, 30, content, view) == 400);
}
