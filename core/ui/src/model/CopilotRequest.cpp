#include "daw/ui/model/CopilotRequest.h"

#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/copilot/Tools.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/ui/model/GhostProposal.h"
#include "daw/ui/model/LaneEditing.h"

#include <algorithm>
#include <utility>

namespace daw::ui::copilot
{
namespace
{

// The lines the screen takes away with a removal (S18): the track line left
// empty by its last audio clip, or by its track. The copilot's audio.remove
// and track.remove leave the same project as the screen's, in the same
// history entry; what the commands themselves do does not change.
std::vector<std::unique_ptr<domain::Command>> linesLeftBy(const domain::ProjectState& before,
                                                          const domain::Command& command)
{
    std::vector<domain::LaneId> lines;
    const auto payload = command.payload();
    if (command.type() == domain::RemoveAudio::commandType)
    {
        if (const auto text = payload.stringAt("clipId"); text)
            if (const auto clipId = domain::AudioClipId::parse(text.value()); clipId)
                lines = laneEditing::linesLeftEmpty(before, {clipId.value()}, {});
    }
    else if (command.type() == domain::RemoveTrack::commandType)
    {
        if (const auto text = payload.stringAt("trackId"); text)
            if (const auto trackId = domain::TrackId::parse(text.value()); trackId)
                lines = laneEditing::linesLeftEmpty(before, {}, {}, trackId.value());
    }

    std::vector<std::unique_ptr<domain::Command>> removals;
    for (const auto laneId : lines)
        removals.push_back(std::make_unique<domain::RemoveLane>(laneId));
    return removals;
}

} // namespace

domain::Result<std::vector<std::unique_ptr<domain::Command>>>
generateCommands(const domain::ProjectState& state,
                 const domain::Value& payload,
                 const domain::generation::StyleModel& model)
{
    auto clipText = payload.stringAt("clipId");
    if (!clipText)
        return clipText.error();
    auto clipId = domain::ClipId::parse(clipText.value());
    if (!clipId)
        return domain::fail(clipId.error().code, "clipId: " + clipId.error().message);

    const auto* clip = state.findClip(clipId.value());
    auto patternId = state.patternOfClip(clipId.value());
    if (clip == nullptr || !patternId)
        return domain::fail(domain::ErrorCode::notFound, "clipId: no such row");
    const auto* pattern = state.findPattern(patternId.value());
    if (pattern == nullptr)
        return domain::fail(domain::ErrorCode::notFound, "clipId: no pattern holds this row");

    const auto number = [&payload](std::string_view key, double fallback) -> domain::Result<double>
    {
        const auto* found = payload.find(key);
        if (found == nullptr || found->isNull())
            return fallback;
        return found->asDouble();
    };
    auto from = number("fromBeats", 0.0);
    auto to = number("toBeats", pattern->lengthBeats);
    if (!from || !to)
        return domain::fail(domain::ErrorCode::invalidPayload, "fromBeats and toBeats must be numbers");

    // The constraints are the S14 contract, read by its own reader: the keys
    // that are not constraints (clipId, the range, the variant) are not its.
    auto constraints = domain::generation::Constraints::fromValue(payload);
    if (!constraints)
        return constraints.error();

    auto variant = 1;
    if (const auto* found = payload.find("variant"); found != nullptr && !found->isNull())
    {
        auto asked = found->asInt();
        if (!asked || asked.value() < 1 || asked.value() > domain::generation::Variants::maxVariants)
            return domain::fail(domain::ErrorCode::invalidPayload, "variant must be an integer from 1 to 16");
        variant = static_cast<int>(asked.value());
    }

    domain::generation::Interpretation interpretation{};
    interpretation.constraints = std::move(constraints).value();
    auto opened = GhostProposal::open(
        state, patternId.value(), clip->trackId, from.value(), to.value(), std::move(interpretation), model);
    if (!opened)
        return opened.error();

    auto proposal = std::move(opened).value();
    static_cast<void>(proposal.shift(variant - 1));
    if (proposal.notes().empty())
        return domain::fail(domain::ErrorCode::invalidArgument,
                            "the generator has nothing to propose in this range");

    auto acceptance = proposal.accept(state, clipId.value());
    return std::move(acceptance.commands);
}

Expansion expand(const domain::ProjectState& state,
                 const domain::CommandRegistry& registry,
                 const std::vector<domain::CommandQueue::Step>& steps,
                 const domain::generation::StyleModel& model)
{
    Expansion out{};
    auto copy = state;

    const auto refuse = [&out](std::size_t index, const domain::Error& error)
    { out.refusal = Refusal{index, error.code, error.message}; };

    for (std::size_t index = 0; index < steps.size(); ++index)
    {
        const auto& step = steps[index];
        std::vector<std::unique_ptr<domain::Command>> commands;

        if (step.type == domain::copilot::generationToolName)
        {
            auto generated = generateCommands(copy, step.payload, model);
            if (!generated)
            {
                refuse(index, generated.error());
                return out;
            }
            commands = std::move(generated).value();
        }
        else
        {
            auto created = registry.create(step.type, step.payload);
            if (!created)
            {
                refuse(index, created.error());
                return out;
            }
            auto lines = linesLeftBy(copy, *created.value());
            commands.push_back(std::move(created).value());
            for (auto& line : lines)
                commands.push_back(std::move(line));
        }

        for (const auto& command : commands)
        {
            if (auto applied = command->apply(copy); !applied)
            {
                refuse(index, applied.error());
                return out;
            }
            out.steps.push_back(domain::CommandQueue::Step{std::string{command->type()}, command->payload()});
        }
    }
    return out;
}

} // namespace daw::ui::copilot
