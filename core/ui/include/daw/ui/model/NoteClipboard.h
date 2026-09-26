#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace daw::ui
{

// What Ctrl+C takes in the piano roll and in the channel rack: notes, by
// value, never a selection of the screen.
//
// Values and not identifiers: a copied note is a pitch, a velocity, a length
// and a start relative to the copy, so it survives a change of pattern, and
// even the removal of the note it was copied from. The track of each row is
// kept as the place a paste goes when nothing says otherwise.
//
// It lives with the interface, not in the domain, as validated at S11: it is
// not project state — not journalled, not undone, not saved — and a copilot
// has no use for it, since it writes notes by value already.
struct CopiedRow
{
    domain::TrackId track{};
    std::vector<domain::Note> notes; // startBeats relative to the copy's origin
};

struct CopiedNotes
{
    std::vector<CopiedRow> rows;

    // From the origin to the end of the last note: what Ctrl+B skips over,
    // rounded up to the bar.
    double spanBeats{0.0};

    // Where, in its pattern, the copy started: where Ctrl+V puts it back when
    // the playhead is not in the pattern it is pasted into.
    double originBeats{0.0};
};

// The one clipboard the piano roll and the rack share: notes copied in one
// paste in the other.
class Clipboard
{
public:
    std::optional<CopiedNotes> notes;
};

// The piano roll: some notes of one row, the origin at the earliest of them.
[[nodiscard]] CopiedNotes
copyNotes(const domain::Pattern& pattern, domain::TrackId track, const std::vector<domain::NoteId>& noteIds);

// The rack: whole rows of some channels, the origin at the pattern's start.
[[nodiscard]] CopiedNotes copyRows(const domain::Pattern& pattern,
                                   const std::vector<domain::TrackId>& tracks);

// What a paste does, as commands the caller runs in one group — one Ctrl+Z —
// with identifiers drawn here, by the caller of the bus, like everywhere.
struct PastePlan
{
    std::vector<std::unique_ptr<domain::Command>> commands;
    std::vector<domain::NoteId> pasted;

    // Notes left out: on a note of the same pitch and start already there, or
    // past the end of a pattern the paste may not lengthen.
    std::size_t skipped{0};
};

// Row i of the clipboard goes to targets[i] when there is one, to the track
// it was copied from otherwise. A row the pattern lacks is opened.
//
// Notes land at atBeats plus their offset. One starting past the pattern's
// end is left out, unless `lengthen` — what Ctrl+B asks — in which case the
// pattern is lengthened to the bar that holds it, in the same group.
[[nodiscard]] PastePlan planPaste(const domain::ProjectState& state,
                                  domain::PatternId patternId,
                                  const CopiedNotes& copied,
                                  const std::vector<domain::TrackId>& targets,
                                  double atBeats,
                                  bool lengthen);

// Where Ctrl+B puts the copy: right after the copied span, rounded up to the
// bar, from the origin of the copy.
[[nodiscard]] double duplicateAt(const CopiedNotes& copied, double originBeats);

} // namespace daw::ui
