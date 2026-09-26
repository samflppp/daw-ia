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
// The rack chooses the pattern and the channel, the piano roll writes its
// notes. Neither owns the pattern, and neither may hold an answer the other would
// have to duplicate — a second copy of "which pattern are we on" is the very
// thing the S7bis review said not to build.

// The pattern being worked on: the selected one, or the first of the project
// when nothing is selected. Null on a project that holds none.
[[nodiscard]] const domain::Pattern* current(const domain::ProjectState& state, const Selection& selection);

// Where the playhead is inside a pattern, in the pattern's own beats.
//
// Pattern mode plays the auditioned pattern from beat 0, so the transport's
// position is already local. Song mode plays placements, so the position is
// local to the placement it falls in, if any. Nothing when the pattern is not
// what is sounding: the piano roll draws no playhead then, rather
// than one that moves over a pattern nobody hears.
[[nodiscard]] std::optional<double>
localBeats(const domain::ProjectState& state, domain::PatternId patternId, double positionBeats);

// The reverse: where on the transport a beat of the pattern is. In song mode,
// in its earliest placement; in pattern mode, the beat itself.
[[nodiscard]] double
transportBeat(const domain::ProjectState& state, domain::PatternId patternId, double patternBeats);

// Makes pattern mode audition that pattern. Nothing in song mode, where the
// arrangement plays whatever pattern is being edited — which is what song mode
// means. A transient command like every other transport change, so it enters
// no history entry; and nothing at all when it would change nothing.
//
// Never called from inside a bus notification: an observer may not call back
// into the bus.
void follow(domain::CommandBus& bus, const domain::ProjectState& state, domain::PatternId patternId);

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

// The command that creates a pattern, empty and unplaced.
//
// It used to lay the pattern down too, because the loop needed a placement to
// hear it. Pattern mode plays a pattern where it is, so a new pattern stays off
// the arrangement until the playlist puts it there — which is what a beatmaker
// expects, and what keeps the song from filling up with every sketch.
struct NewPattern
{
    domain::PatternId patternId{};
    std::vector<std::unique_ptr<domain::Command>> commands;
};

[[nodiscard]] NewPattern newPattern(double lengthBeats);

// What the pattern is called on screen. A pattern the domain left unnamed is
// shown by its rank: the domain invents no name, because a name invented at
// apply() time would differ between a run and its replay.
[[nodiscard]] std::string displayName(const domain::ProjectState& state, const domain::Pattern& pattern);

} // namespace daw::ui::patternEditing
