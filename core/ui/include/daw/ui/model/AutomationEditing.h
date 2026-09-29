#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"

#include <string>
#include <vector>

namespace daw::ui::automationEditing
{

// What the playlist's automation lanes and the right-click on a slider do with
// a line. Pure: a lane's order, its name, where a value is drawn and what a
// wheel notch does all have a right answer, so a test pins them without a
// window.

// The lines in the order the playlist shows them: the strips in their order —
// channels, then buses, then the master — and, for each, its volume, its pan,
// then the parameters of its plugins in chain order.
[[nodiscard]] std::vector<const domain::AutomationLine*> ordered(const domain::ProjectState& state);

// The strip a line belongs to on screen: its own for a volume or a pan, the
// one holding the plugin for a parameter. Nil when the plugin is not found.
[[nodiscard]] domain::TrackId stripOf(const domain::ProjectState& state, const domain::AutomationLine& line);

// "Kick · Volume", "Master · Pan", "Basse · Vital · 12".
[[nodiscard]] std::string label(const domain::ProjectState& state, const domain::AutomationLine& line);

// Where a value sits in a lane, from 0 at the bottom to 1 at the top, and
// back. The volume is drawn as its fader position, the space it bends in,
// so the line drawn is the line heard.
[[nodiscard]] double heightOf(const domain::AutomationTarget& target, double value) noexcept;
[[nodiscard]] double valueAtHeight(const domain::AutomationTarget& target, double height) noexcept;

// One wheel notch on a point: a dB for a volume, a twentieth of the way for a
// pan, a hundredth for a parameter. Clamped to the target's range.
[[nodiscard]] double stepValue(const domain::AutomationTarget& target, double value, int notches) noexcept;

// One Alt + wheel notch on a point: a tenth of the curve, clamped.
[[nodiscard]] double stepCurve(double curve, int notches) noexcept;

// The value the target holds when no line drives it: its fader, its pan, or
// the plugin parameter as the project stores it (0.5 when never touched).
[[nodiscard]] double staticValue(const domain::ProjectState& state, const domain::AutomationTarget& target);

// What a slider shows: while the song plays, the value its line plays there;
// stopped, in pattern mode (where no automation plays), or with no point to
// follow, the value the project holds. The hand on the slider is the caller's
// business: it shows the hand, not this.
[[nodiscard]] double shownValue(const domain::ProjectState& state,
                                const domain::AutomationTarget& target,
                                bool playing,
                                double positionBeats);

// The right-click on a slider: the target's line, created empty when it has
// none, in one history entry. Nil when the bus refused.
[[nodiscard]] domain::AutomationLineId
open(domain::CommandBus& bus, const domain::ProjectState& state, const domain::AutomationTarget& target);

} // namespace daw::ui::automationEditing
