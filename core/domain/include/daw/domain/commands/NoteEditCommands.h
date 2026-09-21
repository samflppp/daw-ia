#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>
#include <vector>

namespace daw::domain
{

// Editing verbs that work on notes the caller names, and on nothing else.
//
// note.quantize and note.transpose take a list of identifiers rather than
// acting on "the selection". A selection lives in the interface, not in the
// project: a command whose meaning depended on it could not be replayed, could
// not be sent over JSON-RPC, and could not be issued by a copilot that has no
// selection of its own. The interface reads its selection and puts the
// identifiers in the payload; every other caller does the same.
//
// Both are all-or-nothing. A quantize that moved half its notes before
// refusing the other half would leave an undo record describing a state that
// never existed.

// note.set_velocity — changes how hard one note is struck.
//
// A continuous gesture, so it coalesces per note, like note.move and
// note.resize: velocity is dragged, and two notes changed inside one gesture
// stay two entries.
class SetNoteVelocity final : public Command
{
public:
    static constexpr std::string_view commandType = "note.set_velocity";

    SetNoteVelocity(ClipId clipId, NoteId noteId, int velocity);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] NoteId noteId() const noexcept { return noteId_; }
    [[nodiscard]] int velocity() const noexcept { return velocity_; }

private:
    ClipId clipId_;
    NoteId noteId_;
    int velocity_;
};

// note.quantize — snaps the start of each named note onto a grid.
//
// The grid is given in beats, so a sixteenth is 0.25 and a triplet eighth is
// 1/3: the payload carries a duration and never the name of a musical figure,
// which would have to be read against a time signature the domain does not
// hold yet.
//
// Lengths are untouched. Quantizing starts and quantizing durations are two
// different musical decisions, and a command that did both would undo into a
// phrase the user never played.
class QuantizeNotes final : public Command
{
public:
    static constexpr std::string_view commandType = "note.quantize";

    QuantizeNotes(ClipId clipId, std::vector<NoteId> noteIds, double gridBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    ClipId clipId_;
    std::vector<NoteId> noteIds_;
    double gridBeats_;
};

// note.transpose — moves the named notes by a number of semitones.
//
// Refused whole if any note would leave [0, 127]. Clamping instead would make
// a transposition down and back up again return a different chord, and undo
// would not be the inverse of the command.
class TransposeNotes final : public Command
{
public:
    static constexpr std::string_view commandType = "note.transpose";

    TransposeNotes(ClipId clipId, std::vector<NoteId> noteIds, int semitones);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    ClipId clipId_;
    std::vector<NoteId> noteIds_;
    int semitones_;
};

} // namespace daw::domain
