#pragma once

#include <cstddef>
#include <vector>

namespace daw::ui::snap
{

// The pages of a windowed workspace held together by the hand (S21, chantier 2
// of S19): a window dragged near an edge lands on it, and two windows that
// touch are resized together, the way FL's windows behave.
//
// Pure arithmetic on rectangles, in pixels of the desktop: PageWindow asks,
// WorkspaceView places. Nothing here is stored. Which windows are held
// together is read from the edges that touch when a gesture starts, never
// kept: the places of the pages stay what they were, fractions of the desktop.

struct Box
{
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    [[nodiscard]] int right() const noexcept { return x + width; }
    [[nodiscard]] int bottom() const noexcept { return y + height; }
    friend bool operator==(const Box&, const Box&) = default;
};

// The edges a resize moves, or the edge of another window held to one.
enum Edge : unsigned
{
    left = 1U,
    top = 2U,
    right = 4U,
    bottom = 8U
};

// A window dragged to `window`: on each axis, the edge nearest to an edge of
// the desktop or of another window, within `reach` pixels, is put on it. An
// edge of another window counts only where the two face each other.
[[nodiscard]] Box moved(Box window, Box desktop, const std::vector<Box>& others, int reach);

// A window resized to `window` by its `edges`: the edges that move land the
// same way; the others stay where they are.
[[nodiscard]] Box resized(Box window, unsigned edges, Box desktop, const std::vector<Box>& others, int reach);

// Another window held to an edge of the one being resized: its edge lies on
// that edge, and the two face each other over some length.
struct Link
{
    std::size_t other{0}; // index in the list it was read from
    unsigned edge{0};     // the other window's edge that follows
};

// Read when the gesture starts, with the edges it moves.
[[nodiscard]] std::vector<Link> linked(Box window, unsigned edges, const std::vector<Box>& others);

// The window and the others once a held edge moved from `before` to `after`:
// each linked edge follows. A linked window is never made smaller than
// minWidth × minHeight; the edge stops there instead, for both.
struct Followed
{
    Box window;
    std::vector<Box> others;
};
[[nodiscard]] Followed follow(Box before,
                              Box after,
                              const std::vector<Link>& links,
                              std::vector<Box> others,
                              int minWidth,
                              int minHeight);

} // namespace daw::ui::snap
