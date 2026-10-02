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

// What Ctrl+C takes in the piano roll: notes, by value, never a selection of
// the screen. The rack copied whole rows until S12, when it stopped holding
// notes.
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

// The piano roll's clipboard, kept by the application rather than by the
// panel: notes copied in one pattern paste in another, whichever window the
// piano roll is shown in.
class Clipboard
{
public:
    std::optional<CopiedNotes> notes;
};

// The piano roll: some notes of one row, the origin at the earliest of them.
[[nodiscard]] CopiedNotes
copyNotes(const domain::Pattern& pattern, domain::TrackId track, const std::vector<domain::NoteId>& noteIds);

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
// pattern is lengthened to the bar that holds it, in the same group. The bar
// is the project's.
[[nodiscard]] PastePlan planPaste(const domain::ProjectState& state,
                                  domain::PatternId patternId,
                                  const CopiedNotes& copied,
                                  const std::vector<domain::TrackId>& targets,
                                  double atBeats,
                                  bool lengthen);

// The canvas (S19): notes picked in blocks laid anywhere on the song, of one
// pattern or several. Each comes with the beat its block starts on and the
// track of its row. The copy keeps the gaps the ear heard between them, in
// song time: a note picked late in one block and one early in the next stay
// that far apart. Rows by track, in the order the tracks are first met.
struct PickedInBlock
{
    double blockStart{0.0};
    domain::TrackId track{};
    domain::Note note;
};

// The origin is the earliest picked note, in song time; originBeats is
// where that note sits in its own pattern.
[[nodiscard]] CopiedNotes copyFromBlocks(const std::vector<PickedInBlock>& picked);

// Where Ctrl+B puts the copy: right after the copied span, rounded up to the
// bar — barBeats long, from the project's signature — from the origin of the
// copy.
[[nodiscard]] double duplicateAt(const CopiedNotes& copied, double originBeats, double barBeats);

} // namespace daw::ui
