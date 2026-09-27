#pragma once

#include "daw/domain/generation/Constraints.h"

#include <cstdint>
#include <vector>

namespace daw::domain::generation
{

// The structure layer, above the style model and never in its place.
//
// The Markov chain writes one unit -- a bar, or two for a long melody -- and
// the form lays it out over the range. A repeated unit is transformed, never
// redrawn: the same notes shifted, then one of a handful of small changes.
//
//   a        the unit again. When the chord under it is not the chord under
//            the first one, a bass follows the root and a melody moves its
//            strong-beat notes to the nearest chord tone: same rhythm, same
//            contour.
//   aVaried  the unit and one light change: a note moved by one step of the
//            grid, a velocity changed, or the last note closed or opened.
//   b        the first half of the unit kept, the second half drawn again by
//            the style model, with a rhythm that must differ, and an open end.
//   aReturn  the unit again, with its last note closed on the tonic: the A
//            that ends an AABA. Its rhythm is the unit's.
enum class Letter : std::uint8_t
{
    a,
    aVaried,
    b,
    aReturn
};

// How many bars one unit lasts, for a range of that many bars.
[[nodiscard]] int unitBars(Role role, int rangeBars) noexcept;

// The form chosen when the user asked for none, from the role and the number
// of units the range holds.
//
//              1      2     3     4 and more
//   melody     libre  AA'   AAB   AABA
//   bass       libre  AA'   AAB   AAAB
//   rhythm     libre  boucle
//   chords     libre  boucle (the comping rhythm; the progression is per bar)
[[nodiscard]] Form defaultForm(Role role, int units) noexcept;

// The letter of each unit. Empty for the free form and for a single unit:
// there is nothing to repeat. The four-letter forms cycle past four units.
[[nodiscard]] std::vector<Letter> schema(Form form, int units);

} // namespace daw::domain::generation
