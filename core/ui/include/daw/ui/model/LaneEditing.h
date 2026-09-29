#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"

#include <vector>

namespace daw::ui::laneEditing
{

// The lines a removal leaves behind. The line of a track exists for its audio
// clips: once the last one is gone, and if nobody named the line, the playlist
// takes it away in the same history entry, the way pattern.remove takes the
// line of its pattern.
//
// It is the screen's doing, not audio.remove's: changing what that command
// does would change what every journal written since S17 replays into.

// The unnamed track lines that removing these blocks, and this track with all
// its clips when it is not nil, would leave empty. A line that still holds a
// block, or that someone named, stays.
[[nodiscard]] std::vector<domain::LaneId> linesLeftEmpty(const domain::ProjectState& state,
                                                         const std::vector<domain::AudioClipId>& audio,
                                                         const std::vector<domain::PlacementId>& placements,
                                                         domain::TrackId removedTrack = {});

// The playlist's Suppr: these blocks, and the track lines they leave empty,
// in one history entry. False when the bus refused.
bool removeBlocks(domain::CommandBus& bus,
                  const domain::ProjectState& state,
                  const std::vector<domain::AudioClipId>& audio,
                  const std::vector<domain::PlacementId>& placements);

// track.remove, and the line it leaves empty, in one history entry. False
// when the bus refused.
bool removeTrack(domain::CommandBus& bus, const domain::ProjectState& state, domain::TrackId trackId);

} // namespace daw::ui::laneEditing
