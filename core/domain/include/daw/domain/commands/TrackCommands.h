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

// track.set_muted — silences a track, or gives it back.
//
// A switch, not a movement: it never coalesces, exactly like
// plugin.set_bypassed. The two are deliberately distinct commands. Mute is a
// property of the track, used while arranging; bypass is a property of one
// plugin in a chain, used while mixing. A single command for both would force
// a choice the moment a user wants a muted track whose reverb still rings on
// a send, or a bypassed compressor on a track that plays.
class SetTrackMuted final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_muted";

    SetTrackMuted(TrackId trackId, bool muted);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }
    [[nodiscard]] bool muted() const noexcept { return muted_; }

private:
    TrackId trackId_;
    bool muted_;
};

} // namespace daw::domain
