#pragma once

#include "daw/domain/project/ProjectState.h"

#include <optional>
#include <string_view>

namespace daw::ui::tempoEditing
{

// What the transport's tempo and signature readouts, and the playlist's tempo
// lane, do with a wheel notch or a typed value. Pure: the answers have a right
// value, so a test pins them down without a window.

// The project's tempo: the point at the origin. What the readout shows and
// what the wheel changes, whether or not the tempo is automated further on.
[[nodiscard]] double projectTempo(const domain::ProjectState& state) noexcept;

// One notch up or down, by whole beats per minute: 120.5 goes to 121 or 120,
// never to 121.5. Clamped to the range the domain accepts.
[[nodiscard]] double stepTempo(double bpm, int notches) noexcept;

// "140", "97.5", "97,5" (a French keyboard types a comma). Nothing for a text
// that is not a number in the domain's range.
[[nodiscard]] std::optional<double> parseTempo(std::string_view text);

// The numerator, one notch up or down, the denominator kept.
[[nodiscard]] domain::TimeSignature stepSignature(domain::TimeSignature signature, int notches) noexcept;

// "6/8", " 3 / 4 ". Nothing for a text the domain would refuse.
[[nodiscard]] std::optional<domain::TimeSignature> parseSignature(std::string_view text);

// Where "Automatiser le tempo" puts its first point: on the bar of the
// playhead, or on the second bar when the playhead is in the first — the
// origin already holds a point there.
[[nodiscard]] double automationStart(const domain::ProjectState& state, double playheadBeats) noexcept;

// A tempo change in force further than the origin: the lane is shown.
[[nodiscard]] bool isAutomated(const domain::ProjectState& state) noexcept;

} // namespace daw::ui::tempoEditing
