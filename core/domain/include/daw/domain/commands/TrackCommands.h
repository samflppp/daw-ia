#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>
#include <string>

namespace daw::domain
{

// Creating a track was the last mutation that did not go through the bus: the
// application built a Track and pushed it into ProjectState directly. That was
// invisible until the journal became the truth on disk — a project whose
// tracks come from nowhere reopens empty, whatever the journal says.
//
// Like every other command, neither of these engenders an identifier: the
// caller generates the TrackId, or a replay would create a different project
// every time it ran.

// track.add — appends an empty track.
class AddTrack final : public Command
{
public:
    static constexpr std::string_view commandType = "track.add";

    AddTrack(TrackId trackId, std::string name, double volumeDb = 0.0);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }

private:
    TrackId trackId_;
    std::string name_;
    double volumeDb_;
};

// track.remove — takes the track out, clips, plugins and all.
//
// The undo record carries the whole track, not its identifier: a track holds
// clips, notes, plugin instances and captured state digests, and the command
// itself knows none of them.
class RemoveTrack final : public Command
{
public:
    static constexpr std::string_view commandType = "track.remove";

    explicit RemoveTrack(TrackId trackId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
};

} // namespace daw::domain
