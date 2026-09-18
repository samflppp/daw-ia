#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"

#include <memory>

namespace daw::domain
{

// clip.create_midi — creates an empty MIDI clip on a track.
//
// The clip identifier is an argument, not something apply() invents: a payload
// replayed twice must produce the same project, and an identifier drawn at
// execution time would break that.
class CreateMidiClip final : public Command
{
public:
    static constexpr std::string_view commandType = "clip.create_midi";

    CreateMidiClip(TrackId trackId, ClipId clipId, double startBeats, double lengthBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
    ClipId clipId_;
    double startBeats_;
    double lengthBeats_;
};

} // namespace daw::domain
