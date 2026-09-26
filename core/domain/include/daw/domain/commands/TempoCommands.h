#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>

namespace daw::domain
{

// The four verbs of the tempo sequence.
//
// Every one of them names its point by identifier, never by position in beats.
// A payload that designated a point by its beat would aim at a different point
// as soon as another command moved it — and moving a point is one of the four.
//
// The point at the origin is the exception the other three have to know about:
// it cannot be removed and it cannot be moved, because the sequence has to stay
// non-empty and has to start at the timeline origin. Its bpm changes like any
// other, and it is the only point an empty project holds.

// tempo.insert — adds a tempo change on the timeline.
//
// Like every other command, it engenders no identifier: the caller generates
// the TempoPointId, or replaying the same payload would build a different
// project every time.
class InsertTempoPoint final : public Command
{
public:
    static constexpr std::string_view commandType = "tempo.insert";

    InsertTempoPoint(TempoPointId pointId, double startBeats, double beatsPerMinute);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] TempoPointId pointId() const noexcept { return pointId_; }

private:
    TempoPointId pointId_;
    double startBeats_;
    double beatsPerMinute_;
};

// tempo.remove — takes a tempo change out.
//
// The undo record carries the whole point, not its identifier: putting it back
// means putting back its beat and its bpm too, and the command knows neither.
class RemoveTempoPoint final : public Command
{
public:
    static constexpr std::string_view commandType = "tempo.remove";

    explicit RemoveTempoPoint(TempoPointId pointId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TempoPointId pointId_;
};

// tempo.set_bpm — changes how fast one point plays, and nothing else.
//
// The continuous one of the four: a tempo is dragged like a fader, so it
// coalesces per point. Two points changed inside the same gesture stay two
// entries, because undoing one must not move the other.
class SetTempoPointBpm final : public Command
{
public:
    static constexpr std::string_view commandType = "tempo.set_bpm";

    SetTempoPointBpm(TempoPointId pointId, double beatsPerMinute);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] TempoPointId pointId() const noexcept { return pointId_; }
    [[nodiscard]] double beatsPerMinute() const noexcept { return beatsPerMinute_; }

private:
    TempoPointId pointId_;
    double beatsPerMinute_;
};

// tempo.move — moves a tempo change along the timeline, and nothing else.
//
// Its bpm does not change, for the same reason note.move leaves a length
// alone: dragging a point sideways and changing its tempo are two gestures,
// and one command for both would undo into a sequence the user never had.
//
// It coalesces per point, like tempo.set_bpm.
class MoveTempoPoint final : public Command
{
public:
    static constexpr std::string_view commandType = "tempo.move";

    MoveTempoPoint(TempoPointId pointId, double startBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] TempoPointId pointId() const noexcept { return pointId_; }
    [[nodiscard]] double startBeats() const noexcept { return startBeats_; }

private:
    TempoPointId pointId_;
    double startBeats_;
};

// project.set_time_signature — how the beats group into bars.
//
// Moves no note and no clip: a beat is a quarter note whatever the signature,
// so only the bar lines drawn over the music change. Like tempo.set_bpm it
// coalesces, because a wheel turned over the readout sends one command per
// notch and the user undoes the turn, not each notch.
class SetTimeSignature final : public Command
{
public:
    static constexpr std::string_view commandType = "project.set_time_signature";

    explicit SetTimeSignature(TimeSignature signature);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] const TimeSignature& signature() const noexcept { return signature_; }

private:
    TimeSignature signature_;
};

} // namespace daw::domain
