#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/project/ProjectState.h"

#include <optional>
#include <vector>

namespace daw::ui::framing
{

// F frames what exists, Shift+F shows the whole (S18): the same two keys in
// the piano roll, the playlist and the canvas. A piano roll that shows 128 keys
// for a pattern of eight notes makes the hand travel through nothing.
//
// The arithmetic of it, pure: what to frame and how wide a beat must be for it
// to fill the view. The panels only apply the answer.

struct Span
{
    double from{0.0}; // beats
    double to{0.0};
    int low{60}; // pitches, both included
    int high{60};

    friend bool operator==(const Span&, const Span&) = default;
};

// The notes picked, or all of them when none is: where they start and end,
// how low and how high. Nothing when there is no note.
[[nodiscard]] std::optional<Span> notes(const std::vector<domain::Note>& all,
                                        const std::vector<domain::NoteId>& picked);

// The width of a beat that shows the span across `pixels`, a twentieth of room
// on each side, a beat at least, within [narrowest, widest].
[[nodiscard]] double beatWidthFor(const Span& span, int pixels, double narrowest, double widest) noexcept;

// The first beat on the left once that width is chosen: the span centred.
[[nodiscard]] double firstBeatFor(const Span& span, double beatWidth, int pixels) noexcept;

// The highest pitch on screen for a window of `rows` keys: the span centred
// when it fits, its highest note under two semitones of air when it does not.
[[nodiscard]] int topPitchFor(const Span& span, int rows) noexcept;

} // namespace daw::ui::framing
