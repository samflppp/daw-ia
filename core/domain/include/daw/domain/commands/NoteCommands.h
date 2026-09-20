#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <memory>

namespace daw::domain
{

// The three commands a piano roll needs beyond note.add.
//
// They are separate for the same reason mute and bypass are: they are two
// different gestures with two different undo behaviours. Deleting is a click,
// dragging is a movement of sixty frames, and only the second one coalesces.

// note.remove — takes a note out of its clip.
//
// The undo record carries the whole note and its index, not its identifier: a
// note put back at the end of the vector is the same music and a different
// serialized project, and a session undone to its start would no longer equal
// the project it started from.
class RemoveNote final : public Command
{
public:
    static constexpr std::string_view commandType = "note.remove";

    RemoveNote(ClipId clipId, NoteId noteId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    ClipId clipId_;
    NoteId noteId_;
};

// note.move — moves a note in time and in pitch, and nothing else.
//
// Length is deliberately absent. Dragging a note and stretching it are two
// gestures; one command for both would let an undo give back a note the user
// never had — the position of the drag with the length before the stretch.
//
// Coalescing is per note: dragging one note across a bar is one history entry,
// and two notes moved inside the same gesture stay two entries, because undoing
// one must not move the other.
class MoveNote final : public Command
{
public:
    static constexpr std::string_view commandType = "note.move";

    MoveNote(ClipId clipId, NoteId noteId, int pitch, double startBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] NoteId noteId() const noexcept { return noteId_; }
    [[nodiscard]] int pitch() const noexcept { return pitch_; }
    [[nodiscard]] double startBeats() const noexcept { return startBeats_; }

private:
    ClipId clipId_;
    NoteId noteId_;
    int pitch_;
    double startBeats_;
};

// note.resize — changes the length of a note, and nothing else.
//
// Its start stays where it is: stretching from the right edge is the gesture
// the interface offers, and a command that moved the note too would undo into
// a note the user never had.
//
// Coalescing is per note, like note.move: a stretch is a movement of sixty
// frames and has to be one history entry, and two notes stretched inside the
// same gesture stay two entries.
class ResizeNote final : public Command
{
public:
    static constexpr std::string_view commandType = "note.resize";

    ResizeNote(ClipId clipId, NoteId noteId, double lengthBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] NoteId noteId() const noexcept { return noteId_; }
    [[nodiscard]] double lengthBeats() const noexcept { return lengthBeats_; }

private:
    ClipId clipId_;
    NoteId noteId_;
    double lengthBeats_;
};

} // namespace daw::domain
