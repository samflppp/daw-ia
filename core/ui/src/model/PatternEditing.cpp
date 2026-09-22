#include "daw/ui/model/PatternEditing.h"

#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TransportCommands.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace daw::ui::patternEditing
{
namespace
{

// The length a pattern gets when the rack makes one: four bars of four beats.
// The grid the rack draws follows it, never the other way round.
constexpr double defaultPatternLengthBeats = 16.0;

} // namespace

const domain::Pattern* current(const domain::ProjectState& state, const Selection& selection)
{
    if (const auto* named = state.findPattern(selection.pattern()); named != nullptr)
        return named;

    if (state.patterns().empty())
        return nullptr;

    // The first one, so a project holding a single pattern does not need a
    // click before it can be edited.
    return &state.patterns().front();
}

std::optional<Span> spanOf(const domain::ProjectState& state, domain::PatternId patternId)
{
    const auto* pattern = state.findPattern(patternId);
    if (pattern == nullptr)
        return std::nullopt;

    const auto placements = state.placementsOf(patternId);
    if (placements.empty())
        return std::nullopt;

    return Span{placements.front()->startBeats, pattern->lengthBeats};
}

void loopOver(domain::CommandBus& bus, const domain::ProjectState& state, domain::PatternId patternId)
{
    const auto span = spanOf(state, patternId);
    if (!span)
        return;

    static_cast<void>(bus.execute(std::make_unique<domain::TransportSetLoop>(
        true, span->startBeats, span->startBeats + span->lengthBeats)));
}

Row rowFor(const domain::ProjectState& state, domain::PatternId patternId, domain::TrackId trackId)
{
    Row row{};

    const auto* pattern = state.findPattern(patternId);
    if (pattern == nullptr)
        return row;

    if (const auto* existing = pattern->findClipForTrack(trackId); existing != nullptr)
    {
        row.clipId = existing->id;
        return row;
    }

    row.clipId = domain::ClipId::generate();
    row.opening.push_back(std::make_unique<domain::AddPatternTrack>(patternId, row.clipId, trackId));
    return row;
}

NewPattern newPattern(const domain::ProjectState& state, double lengthBeats)
{
    NewPattern created{};
    created.patternId = domain::PatternId::generate();

    const auto length = lengthBeats > 0.0 ? lengthBeats : defaultPatternLengthBeats;

    // After everything already laid down, so a new pattern never starts on top
    // of another one. It is a first position and not a rule: the playlist of
    // the next week moves placements, and moving one is what it is for.
    double start = 0.0;
    for (const auto& placement : state.arrangement())
    {
        const auto* pattern = state.findPattern(placement.patternId);
        if (pattern == nullptr)
            continue;

        start = std::max(start, placement.startBeats + pattern->lengthBeats);
    }

    created.commands.push_back(
        std::make_unique<domain::CreatePattern>(created.patternId, std::string{}, length));
    created.commands.push_back(
        std::make_unique<domain::PlacePattern>(domain::PlacementId::generate(), created.patternId, start));

    return created;
}

std::string displayName(const domain::ProjectState& state, const domain::Pattern& pattern)
{
    if (!pattern.name.empty())
        return pattern.name;

    auto index = state.patternIndex(pattern.id);
    const auto rank = index.ok() ? index.value() + 1 : 1;
    return "Pattern " + std::to_string(rank);
}

} // namespace daw::ui::patternEditing
