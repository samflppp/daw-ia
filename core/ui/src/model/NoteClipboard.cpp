#include "daw/ui/model/NoteClipboard.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/PatternCommands.h"

#include <algorithm>
#include <cmath>

namespace daw::ui
{
namespace
{

// Two starts closer than this are the same start: a sixty-fourth of a
// sixty-fourth, far under any grid.
constexpr double sameStart = 1e-6;

[[nodiscard]] double ceilToBar(double beats, double barBeats)
{
    return std::ceil(beats / barBeats - 1e-9) * barBeats;
}

void measureSpan(CopiedNotes& copied)
{
    copied.spanBeats = 0.0;
    for (const auto& row : copied.rows)
    {
        for (const auto& note : row.notes)
            copied.spanBeats = std::max(copied.spanBeats, note.startBeats + note.lengthBeats);
    }
}

} // namespace

CopiedNotes
copyNotes(const domain::Pattern& pattern, domain::TrackId track, const std::vector<domain::NoteId>& noteIds)
{
    CopiedNotes copied;
    const auto* row = pattern.findClipForTrack(track);
    if (row == nullptr)
        return copied;

    CopiedRow copiedRow{track, {}};
    for (const auto& note : row->notes)
    {
        if (std::find(noteIds.begin(), noteIds.end(), note.id) != noteIds.end())
            copiedRow.notes.push_back(note);
    }
    if (copiedRow.notes.empty())
        return copied;

    double origin = copiedRow.notes.front().startBeats;
    for (const auto& note : copiedRow.notes)
        origin = std::min(origin, note.startBeats);
    for (auto& note : copiedRow.notes)
        note.startBeats -= origin;
    copied.originBeats = origin;

    copied.rows.push_back(std::move(copiedRow));
    measureSpan(copied);
    return copied;
}

CopiedNotes copyRows(const domain::Pattern& pattern, const std::vector<domain::TrackId>& tracks)
{
    CopiedNotes copied;
    for (const auto& track : tracks)
    {
        const auto* row = pattern.findClipForTrack(track);
        copied.rows.push_back(CopiedRow{track, row != nullptr ? row->notes : std::vector<domain::Note>{}});
    }
    measureSpan(copied);
    return copied;
}

PastePlan planPaste(const domain::ProjectState& state,
                    domain::PatternId patternId,
                    const CopiedNotes& copied,
                    const std::vector<domain::TrackId>& targets,
                    double atBeats,
                    bool lengthen)
{
    PastePlan plan;
    const auto* pattern = state.findPattern(patternId);
    if (pattern == nullptr)
        return plan;

    // How long the pattern has to be, and whether it has to grow.
    double length = pattern->lengthBeats;
    if (lengthen)
    {
        for (const auto& row : copied.rows)
        {
            for (const auto& note : row.notes)
            {
                const auto start = atBeats + note.startBeats;
                if (start >= length)
                    length = std::max(length, ceilToBar(start + sameStart, state.beatsPerBar()));
            }
        }
        if (length > pattern->lengthBeats)
            plan.commands.push_back(std::make_unique<domain::SetPatternLength>(patternId, length));
    }

    for (std::size_t index = 0; index < copied.rows.size(); ++index)
    {
        const auto& row = copied.rows[index];
        const auto track = index < targets.size() ? targets[index] : row.track;
        if (state.findTrack(track) == nullptr)
        {
            plan.skipped += row.notes.size();
            continue;
        }

        // The row, opened when the pattern has none for that track.
        const auto* existing = pattern->findClipForTrack(track);
        auto clipId = existing != nullptr ? existing->id : domain::ClipId{};
        std::vector<domain::Note> landed =
            existing != nullptr ? existing->notes : std::vector<domain::Note>{};

        for (const auto& source : row.notes)
        {
            domain::Note note = source;
            note.startBeats = atBeats + source.startBeats;

            if (note.startBeats < 0.0 || note.startBeats >= length)
            {
                ++plan.skipped;
                continue;
            }

            // Never two notes of the same pitch at the same start: a cell of
            // the rack would hold two, and switching it off would leave one.
            const bool doubled =
                std::any_of(landed.begin(),
                            landed.end(),
                            [&note](const domain::Note& other) {
                                return other.pitch == note.pitch &&
                                       std::abs(other.startBeats - note.startBeats) < sameStart;
                            });
            if (doubled)
            {
                ++plan.skipped;
                continue;
            }

            if (clipId.isNil())
            {
                clipId = domain::ClipId::generate();
                plan.commands.push_back(std::make_unique<domain::AddPatternTrack>(patternId, clipId, track));
            }

            note.id = domain::NoteId::generate();
            landed.push_back(note);
            plan.pasted.push_back(note.id);
            plan.commands.push_back(std::make_unique<domain::AddNote>(clipId, note));
        }
    }

    // A lengthening with nothing pasted after it is a change nobody asked for.
    if (plan.pasted.empty())
        plan.commands.clear();

    return plan;
}

double duplicateAt(const CopiedNotes& copied, double originBeats, double barBeats)
{
    return originBeats + std::max(barBeats, ceilToBar(copied.spanBeats, barBeats));
}

} // namespace daw::ui
