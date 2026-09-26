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

// The length a pattern gets when the rack or the piano roll makes one: four
// bars of four beats.
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

std::optional<double>
localBeats(const domain::ProjectState& state, domain::PatternId patternId, double positionBeats)
{
    const auto* pattern = state.findPattern(patternId);
    if (pattern == nullptr)
        return std::nullopt;

    const auto& transport = state.transport();
    if (transport.mode == domain::PlayMode::pattern)
    {
        if (transport.auditionedPattern != patternId || positionBeats < 0.0 ||
            positionBeats >= pattern->lengthBeats)
            return std::nullopt;

        return positionBeats;
    }

    for (const auto* placement : state.placementsOf(patternId))
    {
        const auto local = positionBeats - placement->startBeats;
        if (local >= 0.0 && local < pattern->lengthBeats)
            return local;
    }

    return std::nullopt;
}

double transportBeat(const domain::ProjectState& state, domain::PatternId patternId, double patternBeats)
{
    if (state.transport().mode == domain::PlayMode::pattern)
        return patternBeats;

    const auto placements = state.placementsOf(patternId);
    return placements.empty() ? patternBeats : placements.front()->startBeats + patternBeats;
}

void follow(domain::CommandBus& bus, const domain::ProjectState& state, domain::PatternId patternId)
{
    const auto& transport = state.transport();
    if (transport.mode != domain::PlayMode::pattern || transport.auditionedPattern == patternId)
        return;

    static_cast<void>(
        bus.execute(std::make_unique<domain::TransportSetMode>(domain::PlayMode::pattern, patternId)));
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

NewPattern newPattern(double lengthBeats)
{
    NewPattern created{};
    created.patternId = domain::PatternId::generate();

    const auto length = lengthBeats > 0.0 ? lengthBeats : defaultPatternLengthBeats;
    created.commands.push_back(
        std::make_unique<domain::CreatePattern>(created.patternId, std::string{}, length));

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
