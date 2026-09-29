#include "daw/ui/model/LaneEditing.h"

#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"

#include <algorithm>
#include <memory>

namespace daw::ui::laneEditing
{

std::vector<domain::LaneId> linesLeftEmpty(const domain::ProjectState& state,
                                           const std::vector<domain::AudioClipId>& audio,
                                           const std::vector<domain::PlacementId>& placements,
                                           domain::TrackId removedTrack)
{
    const auto goes = [&](const domain::AudioClip& clip)
    { return clip.trackId == removedTrack || std::find(audio.begin(), audio.end(), clip.id) != audio.end(); };

    // The lines that could be left behind: each track line a removed clip sat
    // on, and the removed track's own.
    std::vector<domain::LaneId> candidates;
    for (const auto& clip : state.audioClips())
        if (goes(clip) && clip.laneId == domain::ProjectState::laneOfTrack(clip.trackId))
            candidates.push_back(clip.laneId);
    if (!removedTrack.isNil())
        candidates.push_back(domain::ProjectState::laneOfTrack(removedTrack));

    std::vector<domain::LaneId> left;
    for (const auto laneId : candidates)
    {
        if (std::find(left.begin(), left.end(), laneId) != left.end())
            continue;

        const auto* lane = state.findLane(laneId);
        if (lane == nullptr || !lane->name.empty())
            continue;

        const auto keptPlacement = std::any_of(
            state.arrangement().begin(),
            state.arrangement().end(),
            [&](const domain::Placement& placement)
            {
                return placement.laneId == laneId &&
                       std::find(placements.begin(), placements.end(), placement.id) == placements.end();
            });
        const auto keptClip =
            std::any_of(state.audioClips().begin(),
                        state.audioClips().end(),
                        [&](const domain::AudioClip& clip) { return clip.laneId == laneId && !goes(clip); });
        if (!keptPlacement && !keptClip)
            left.push_back(laneId);
    }
    return left;
}

bool removeBlocks(domain::CommandBus& bus,
                  const domain::ProjectState& state,
                  const std::vector<domain::AudioClipId>& audio,
                  const std::vector<domain::PlacementId>& placements)
{
    std::vector<std::unique_ptr<domain::Command>> commands;
    for (const auto clipId : audio)
        commands.push_back(std::make_unique<domain::RemoveAudio>(clipId));
    for (const auto placementId : placements)
        commands.push_back(std::make_unique<domain::RemovePlacement>(placementId));
    for (const auto laneId : linesLeftEmpty(state, audio, placements))
        commands.push_back(std::make_unique<domain::RemoveLane>(laneId));
    if (commands.empty())
        return true;

    domain::GroupOptions group{};
    group.label = audio.size() + placements.size() == 1 ? "retirer un bloc" : "retirer la sélection";
    return bus.executeGroup(std::move(commands), group).ok();
}

bool removeTrack(domain::CommandBus& bus, const domain::ProjectState& state, domain::TrackId trackId)
{
    const auto lines = linesLeftEmpty(state, {}, {}, trackId);
    if (lines.empty())
        return bus.execute(std::make_unique<domain::RemoveTrack>(trackId)).ok();

    std::vector<std::unique_ptr<domain::Command>> commands;
    commands.push_back(std::make_unique<domain::RemoveTrack>(trackId));
    for (const auto laneId : lines)
        commands.push_back(std::make_unique<domain::RemoveLane>(laneId));

    domain::GroupOptions group{};
    group.label = "retirer la piste";
    return bus.executeGroup(std::move(commands), group).ok();
}

} // namespace daw::ui::laneEditing
