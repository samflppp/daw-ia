#include "daw/domain/commands/AddNote.h"

namespace daw::domain
{

AddNote::AddNote(ClipId clipId, Note note)
    : clipId_{clipId}
    , note_{note}
{
}

Result<std::unique_ptr<Command>> AddNote::fromPayload(const Value& payload)
{
    auto clipText = payload.stringAt("clipId");
    if (!clipText)
        return clipText.error();

    auto clipId = ClipId::parse(clipText.value());
    if (!clipId)
        return fail(clipId.error().code, "clipId: " + clipId.error().message);

    // The note keeps the same shape here as in the project snapshot: one
    // reader, one writer, no second dialect to keep in sync.
    auto note = Note::fromValue(payload);
    if (!note)
        return note.error();

    return std::unique_ptr<Command>{new AddNote{clipId.value(), note.value()}};
}

Value AddNote::payload() const
{
    auto value = note_.toValue();
    auto stored = value.set("clipId", Value{clipId_.toString()});
    static_cast<void>(stored); // note_.toValue() is an object, set cannot fail
    return value;
}

Result<Value> AddNote::apply(ProjectState& state) const
{
    auto added = state.addNote(clipId_, note_);
    if (!added)
        return added.error();

    return Value::object({{"clipId", Value{clipId_.toString()}}, {"noteId", Value{note_.id.toString()}}});
}

Result<void> AddNote::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipText = undoRecord.stringAt("clipId");
    if (!clipText)
        return clipText.error();

    auto clipId = ClipId::parse(clipText.value());
    if (!clipId)
        return fail(clipId.error().code, "clipId: " + clipId.error().message);

    auto noteText = undoRecord.stringAt("noteId");
    if (!noteText)
        return noteText.error();

    auto noteId = NoteId::parse(noteText.value());
    if (!noteId)
        return fail(noteId.error().code, "noteId: " + noteId.error().message);

    return state.removeNote(clipId.value(), noteId.value());
}

} // namespace daw::domain
