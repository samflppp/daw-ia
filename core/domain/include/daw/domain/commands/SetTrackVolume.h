#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"

#include <memory>

namespace daw::domain
{

// track.set_volume — sets the volume of a track, in decibels.
//
// The continuous case, and the only one of the three that accepts coalescing:
// dragging a fader emits one command per frame, and the history must keep one
// entry for the whole drag.
class SetTrackVolume final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_volume";

    SetTrackVolume(TrackId trackId, double volumeDb);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    // Absorbs another volume change on the same track, and nothing else.
    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }
    [[nodiscard]] double volumeDb() const noexcept { return volumeDb_; }

private:
    TrackId trackId_;
    double volumeDb_;
};

} // namespace daw::domain
