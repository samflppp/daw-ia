#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>
#include <string>

namespace daw::domain
{

// The verbs of the pattern model.
//
// A pattern is content; a placement is where that content is played. Keeping
// them apart is what makes "modify a pattern laid eight times" one command:
// the eight placements hold no note, so there is nothing to modify eight
// times. Everything the playlist does — laying a pattern again, dragging it,
// taking it off — is a placement and never a note.
//
// There is no placement.resize. The length belongs to the pattern, and
// stretching one laying out of eight must not stretch the seven others. A
// laying cut short would be a length of its own on the placement: additive,
// and not needed yet.

// pattern.create — an empty pattern, of a given length.
//
// Empty and not "one row per track": a pattern that filled itself from the
// track list would mean something different depending on when it ran, and a
// replayed payload must not.
class CreatePattern final : public Command
{
public:
    static constexpr std::string_view commandType = "pattern.create";

    CreatePattern(PatternId patternId, std::string name, double lengthBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] PatternId patternId() const noexcept { return patternId_; }

private:
    PatternId patternId_;
    std::string name_;
    double lengthBeats_;
};

// pattern.place — lays a pattern on the timeline at a beat.
//
// The placement carries no length and no track: the pattern already says both.
// Laying the same pattern eight times is eight of these and copies no note,
// which is the whole reason the model was split.
class PlacePattern final : public Command
{
public:
    static constexpr std::string_view commandType = "pattern.place";

    PlacePattern(PlacementId placementId, PatternId patternId, double startBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] PlacementId placementId() const noexcept { return placementId_; }

private:
    PlacementId placementId_;
    PatternId patternId_;
    double startBeats_;
};

// pattern.add_track — opens a track's row in a pattern.
//
// The row starts empty. Lighting the first cell of a track that has no row yet
// is therefore two commands in one group, and one Ctrl+Z: the row, then the
// note. Splitting them is what keeps note.add meaning exactly one thing.
class AddPatternTrack final : public Command
{
public:
    static constexpr std::string_view commandType = "pattern.add_track";

    AddPatternTrack(PatternId patternId, ClipId clipId, TrackId trackId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] ClipId clipId() const noexcept { return clipId_; }

private:
    PatternId patternId_;
    ClipId clipId_;
    TrackId trackId_;
};

// pattern.set_length — how long the pattern is, in beats.
//
// The grid of the channel rack follows this value; it does not set it. That is
// the choice the S7bis review named: the pattern stays the truth, and the
// number of steps on screen is a reading of it.
//
// Notes past the new length are kept, never cut. Shortening a pattern by
// accident and undoing it has to give them back.
//
// Coalesces per pattern: dragging the length through several bars is one
// history entry.
class SetPatternLength final : public Command
{
public:
    static constexpr std::string_view commandType = "pattern.set_length";

    SetPatternLength(PatternId patternId, double lengthBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] PatternId patternId() const noexcept { return patternId_; }

private:
    PatternId patternId_;
    double lengthBeats_;
};

// pattern.rename — what the pattern is called. An empty name is allowed and
// means "shown by its rank", exactly as at creation.
class RenamePattern final : public Command
{
public:
    static constexpr std::string_view commandType = "pattern.rename";

    RenamePattern(PatternId patternId, std::string name);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PatternId patternId_;
    std::string name_;
};

// pattern.remove — the pattern, its rows, and every placement of it.
//
// A placement of a pattern that is gone would name nothing, so they leave
// together. The undo record carries all of it, with the ranks, so an undo puts
// back the same arrangement and not merely an equivalent one.
class RemovePattern final : public Command
{
public:
    static constexpr std::string_view commandType = "pattern.remove";

    explicit RemovePattern(PatternId patternId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PatternId patternId_;
};

// placement.move — lays an existing placement at another beat.
//
// Coalesces per placement: dragging a block across the playlist is one
// history entry, and the undo puts it back where the drag started.
class MovePlacement final : public Command
{
public:
    static constexpr std::string_view commandType = "placement.move";

    MovePlacement(PlacementId placementId, double startBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

private:
    PlacementId placementId_;
    double startBeats_;
};

// placement.remove — takes one laying off the timeline. The pattern stays: it
// is material, and the seven other layings still play it.
class RemovePlacement final : public Command
{
public:
    static constexpr std::string_view commandType = "placement.remove";

    explicit RemovePlacement(PlacementId placementId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PlacementId placementId_;
};

} // namespace daw::domain
