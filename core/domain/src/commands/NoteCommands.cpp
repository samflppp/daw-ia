#include "daw/domain/commands/NoteCommands.h"

namespace daw::domain
{
namespace
{

Result<ClipId> clipIdAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto id = ClipId::parse(text.value());
    if (!id)
        return fail(id.error().code, std::string{key} + ": " + id.error().message);

    return id.value();
}

Result<NoteId> noteIdAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto id = NoteId::parse(text.value());
    if (!id)
        return fail(id.error().code, std::string{key} + ": " + id.error().message);

    return id.value();
}

} // namespace

// --- note.remove -----------------------------------------------------------

RemoveNote::RemoveNote(ClipId clipId, NoteId noteId)
    : clipId_{clipId}
    , noteId_{noteId}
{
}

Result<std::unique_ptr<Command>> RemoveNote::fromPayload(const Value& payload)
{
    auto clipId = clipIdAt(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteId = noteIdAt(payload, "noteId");
    if (!noteId)
        return noteId.error();

    return std::unique_ptr<Command>{new RemoveNote{clipId.value(), noteId.value()}};
}

Value RemoveNote::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}}, {"noteId", Value{noteId_.toString()}}});
}

Result<Value> RemoveNote::apply(ProjectState& state) const
{
    const auto* clip = state.findClip(clipId_);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId_.toString());

    auto index = state.noteIndex(clipId_, noteId_);
    if (!index)
        return index.error();

    // Read before the removal, because after it the note is gone and nothing
    // else in the project remembers what it was.
    const auto note = clip->notes[index.value()];

    auto removed = state.removeNote(clipId_, noteId_);
    if (!removed)
        return removed.error();

    auto record = note.toValue();
    auto storedClip = record.set("clipId", Value{clipId_.toString()});
    auto storedIndex = record.set("index", Value{static_cast<std::int64_t>(index.value())});
    static_cast<void>(storedClip); // note.toValue() is an object, set cannot fail
    static_cast<void>(storedIndex);
    return record;
}

Result<void> RemoveNote::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = clipIdAt(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    auto note = Note::fromValue(undoRecord);
    if (!note)
        return note.error();

    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();

    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is not negative");

    return state.insertNote(clipId.value(), note.value(), static_cast<std::size_t>(index.value()));
}

// --- note.move -------------------------------------------------------------

MoveNote::MoveNote(ClipId clipId, NoteId noteId, int pitch, double startBeats)
    : clipId_{clipId}
    , noteId_{noteId}
    , pitch_{pitch}
    , startBeats_{startBeats}
{
}

Result<std::unique_ptr<Command>> MoveNote::fromPayload(const Value& payload)
{
    auto clipId = clipIdAt(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteId = noteIdAt(payload, "noteId");
    if (!noteId)
        return noteId.error();

    auto pitch = payload.intAt("pitch");
    if (!pitch)
        return pitch.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    return std::unique_ptr<Command>{
        new MoveNote{clipId.value(), noteId.value(), static_cast<int>(pitch.value()), startBeats.value()}};
}

Value MoveNote::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"noteId", Value{noteId_.toString()}},
                          {"pitch", Value{static_cast<std::int64_t>(pitch_)}},
                          {"startBeats", Value{startBeats_}}});
}

Result<Value> MoveNote::apply(ProjectState& state) const
{
    auto index = state.noteIndex(clipId_, noteId_);
    if (!index)
        return index.error();

    const auto* clip = state.findClip(clipId_);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId_.toString());

    const auto before = clip->notes[index.value()];

    auto moved = state.moveNote(clipId_, noteId_, pitch_, startBeats_);
    if (!moved)
        return moved.error();

    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"noteId", Value{noteId_.toString()}},
                          {"previousPitch", Value{static_cast<std::int64_t>(before.pitch)}},
                          {"previousStartBeats", Value{before.startBeats}}});
}

Result<void> MoveNote::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = clipIdAt(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteId = noteIdAt(undoRecord, "noteId");
    if (!noteId)
        return noteId.error();

    auto pitch = undoRecord.intAt("previousPitch");
    if (!pitch)
        return pitch.error();

    auto startBeats = undoRecord.doubleAt("previousStartBeats");
    if (!startBeats)
        return startBeats.error();

    return state.moveNote(
        clipId.value(), noteId.value(), static_cast<int>(pitch.value()), startBeats.value());
}

bool MoveNote::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const MoveNote*>(&newer);
    return other != nullptr && other->noteId_ == noteId_ && other->clipId_ == clipId_;
}

} // namespace daw::domain
