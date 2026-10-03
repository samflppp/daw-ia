#pragma once

namespace daw::ui::listScroll
{

// The channel rack and the history scroll the way every panel does since S18
// (S21, chantier 2 of S19): the wheel moves the list, the middle button drags
// it, F brings the chosen row into view. A list never zooms: there is nothing
// to zoom.
//
// One offset in pixels from the top of the list, never stored: a state of the screen,
// like the view of the playlist.

// Rows one unit of the wheel moves, the playlist's figure for its lines.
inline constexpr double rowsPerWheelUnit = 8.0;

// Between the top of the list and the last row at the bottom of the view.
[[nodiscard]] int clamped(int offset, int content, int view) noexcept;

// The wheel turned by `deltaY` units: up shows what is above.
[[nodiscard]] int wheeled(int offset, double deltaY, int rowHeight, int content, int view) noexcept;

// The smallest move that shows [top, bottom) whole, or the top when it cannot.
[[nodiscard]] int framed(int offset, int top, int bottom, int content, int view) noexcept;

// A list shown newest first, `added` rows put on top of it: at the top it
// stays at the top and shows them; scrolled down by the hand, it keeps the
// rows it showed.
[[nodiscard]] int afterAdding(int offset, int added, int rowHeight, int content, int view) noexcept;

} // namespace daw::ui::listScroll
