#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/command/CommandQueue.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace daw::ui::copilot
{

// A copilot request, tried on a copy of the project before anything is
// applied.
//
// Each step is created by the registry and applied to the copy, in order, so
// a step sees what the steps before it made: the track, the pattern and the
// row created two calls earlier exist when pattern.generate writes into them.
// The first step the copy refuses is named by its rank, and the model can be
// told which of its calls to correct, in the same request, before the project
// has moved at all.
//
// pattern.generate is turned here into the note.remove and note.add the
// generator's proposal would write on Tab (GhostProposal::accept). An
// audio.remove or a track.remove that leaves its track's line empty is
// followed by the lane.remove the screen would add (laneEditing, S19). What
// reaches the bus is only commands of the registry.
struct Refusal
{
    std::size_t index{0}; // the step, in the order the model sent them
    domain::ErrorCode code{domain::ErrorCode::invalidPayload};
    std::string message;
};

struct Expansion
{
    std::vector<domain::CommandQueue::Step> steps; // what the bus will run
    std::optional<Refusal> refusal;

    [[nodiscard]] bool ok() const noexcept { return !refusal.has_value(); }
};

[[nodiscard]] Expansion expand(const domain::ProjectState& state,
                               const domain::CommandRegistry& registry,
                               const std::vector<domain::CommandQueue::Step>& steps,
                               const domain::generation::StyleModel& model);

// The note.remove and note.add one pattern.generate stands for, in `state`.
[[nodiscard]] domain::Result<std::vector<std::unique_ptr<domain::Command>>>
generateCommands(const domain::ProjectState& state,
                 const domain::Value& payload,
                 const domain::generation::StyleModel& model);

} // namespace daw::ui::copilot
