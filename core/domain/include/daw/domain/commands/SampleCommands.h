#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>
#include <optional>

namespace daw::domain
{

// The verbs of samples: a sampler channel, and audio clips on the timeline.
//
// None of them reads a file. The bytes of a sample reach the project's content
// store before any of these runs — the way a plugin's state does — and the
// payload names them by digest. A replay therefore needs the store and never
// the drumkit the sample came from, and a command still engenders nothing.

// track.set_sample — makes a track a sampler channel on a sample, or, with a
// null sample, gives it back to its chain.
class SetTrackSample final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_sample";

    SetTrackSample(TrackId trackId, std::optional<SampleRef> sample);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
    std::optional<SampleRef> sample_;
};

// audio.place — lays a sample on the timeline, on a track, at a beat, and on
// a playlist line. A nil line — or a payload written before S17 — files it on
// the line of its track, created if missing.
class PlaceAudio final : public Command
{
public:
    static constexpr std::string_view commandType = "audio.place";

    PlaceAudio(AudioClipId clipId, TrackId trackId, SampleRef sample, double startBeats, LaneId laneId = {});

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] AudioClipId clipId() const noexcept { return clipId_; }

private:
    AudioClipId clipId_;
    TrackId trackId_;
    SampleRef sample_;
    double startBeats_;
    LaneId laneId_;
};

// audio.move — another beat for an audio clip, and since S17 another line; a
// nil line keeps the one it had. Coalesces per clip, so a drag is one history
// entry.
class MoveAudio final : public Command
{
public:
    static constexpr std::string_view commandType = "audio.move";

    MoveAudio(AudioClipId clipId, double startBeats, LaneId laneId = {});

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

private:
    AudioClipId clipId_;
    double startBeats_;
    LaneId laneId_;
};

// audio.remove — takes an audio clip off the timeline. The sample's bytes stay
// in the store: an undo, or another clip of the same sample, still needs them.
class RemoveAudio final : public Command
{
public:
    static constexpr std::string_view commandType = "audio.remove";

    explicit RemoveAudio(AudioClipId clipId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    AudioClipId clipId_;
};

} // namespace daw::domain
