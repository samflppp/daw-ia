#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/Selection.h"

#include <optional>
#include <string>
#include <vector>

namespace daw::ui::patternEditing
{

// What the channel rack and the piano roll both need to say, said once.
//
// The two panels are two views of the same pattern: a lit cell in the rack is
// a note in a row, and the same note drawn as a rectangle in the piano roll.
// Neither owns the pattern, and neither may hold an answer the other would
// have to duplicate — a second copy of "which pattern are we on" is the very
// thing the S7bis review said not to build.

// The pattern being worked on: the selected one, or the first of the project
// when nothing is selected. Null on a project that holds none.
[[nodiscard]] const domain::Pattern* current(const domain::ProjectState& state, const Selection& selection);

// Where a pattern is laid on the timeline, and how long it runs. Read from its
// earliest placement; nothing when the pattern has never been laid down, which
// is a pattern that is written and silent.
struct Span
{
    double startBeats{0.0};
    double lengthBeats{4.0};
};

[[nodiscard]] std::optional<Span> spanOf(const domain::ProjectState& state, domain::PatternId patternId);

// Loops the transport over a pattern. A transient command like every other
// transport change, so it enters no history entry.
void loopOver(domain::CommandBus& bus, const domain::ProjectState& state, domain::PatternId patternId);

// The row a track plays in a pattern, creating it when it has none.
//
// Returns the identifier of that row, and the commands that had to run to open
// it — never executed here. The caller puts them in front of its own edit, in
// one group: lighting the first cell of a channel is "open the row, then add
// the note", and that is one thing the user did, so it is one Ctrl+Z.
struct Row
{
    domain::ClipId clipId{};
    std::vector<std::unique_ptr<domain::Command>> opening;
};

[[nodiscard]] Row
rowFor(const domain::ProjectState& state, domain::PatternId patternId, domain::TrackId trackId);

// The commands that create a pattern and lay it down at the first free beat.
// Two commands, one group, one Ctrl+Z.
struct NewPattern
{
    domain::PatternId patternId{};
    std::vector<std::unique_ptr<domain::Command>> commands;
};

[[nodiscard]] NewPattern newPattern(const domain::ProjectState& state, double lengthBeats);

// What the pattern is called on screen. A pattern the domain left unnamed is
// shown by its rank: the domain invents no name, because a name invented at
// apply() time would differ between a run and its replay.
[[nodiscard]] std::string displayName(const domain::ProjectState& state, const domain::Pattern& pattern);

} // namespace daw::ui::patternEditing
