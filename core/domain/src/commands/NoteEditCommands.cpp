#include "daw/domain/commands/NoteEditCommands.h"

#include <cmath>
#include <cstdint>
#include <utility>

namespace daw::domain
{
namespace
{

template <typename IdType>
Result<IdType> idAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto parsed = IdType::parse(text.value());
    if (!parsed)
        return fail(parsed.error().code, std::string{key} + ": " + parsed.error().message);

    return parsed.value();
}

Result<std::vector<NoteId>> noteIdsAt(const Value& value, std::string_view key)
{
    const auto* found = value.find(key);
    if (found == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: " + std::string{key});

    const auto* items = found->asArray();
    if (items == nullptr)
        return fail(ErrorCode::invalidPayload, std::string{key} + " must be an array");

    if (items->empty())
        return fail(ErrorCode::invalidPayload, std::string{key} + " names no note");

    std::vector<NoteId> ids;
    ids.reserve(items->size());
    for (const auto& item : *items)
    {
        auto text = item.asString();
        if (!text)
            return fail(text.error().code, std::string{key} + ": " + text.error().message);

        auto parsed = NoteId::parse(text.value());
        if (!parsed)
            return fail(parsed.error().code, std::string{key} + ": " + parsed.error().message);

        ids.push_back(parsed.value());
    }

    return ids;
}

Value toValue(const std::vector<NoteId>& ids)
{
    Value::Array items;
    items.reserve(ids.size());
    for (const auto& id : ids)
        items.emplace_back(id.toString());
    return Value::array(std::move(items));
}

// The note as the clip holds it right now. Every verb below needs it twice:
// once to refuse before touching anything, once to write the undo record.
Result<Note> findNote(const ProjectState& state, ClipId clipId, NoteId noteId)
{
    const auto* clip = state.findClip(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId.toString());

    for (const auto& note : clip->notes)
    {
        if (note.id == noteId)
            return note;
    }

    return fail(ErrorCode::notFound, "no such note: " + noteId.toString());
}

} // namespace

// ---------------------------------------------------------------------------
// note.set_velocity
// ---------------------------------------------------------------------------

SetNoteVelocity::SetNoteVelocity(ClipId clipId, NoteId noteId, int velocity)
    : clipId_{clipId}
    , noteId_{noteId}
    , velocity_{velocity}
{
}

Result<std::unique_ptr<Command>> SetNoteVelocity::fromPayload(const Value& payload)
{
    auto clipId = idAt<ClipId>(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteId = idAt<NoteId>(payload, "noteId");
    if (!noteId)
        return noteId.error();

    auto velocity = payload.intAt("velocity");
    if (!velocity)
        return velocity.error();

    return std::unique_ptr<Command>{
        new SetNoteVelocity{clipId.value(), noteId.value(), static_cast<int>(velocity.value())}};
}

Value SetNoteVelocity::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"noteId", Value{noteId_.toString()}},
                          {"velocity", Value{velocity_}}});
}

Result<Value> SetNoteVelocity::apply(ProjectState& state) const
{
    auto note = findNote(state, clipId_, noteId_);
    if (!note)
        return note.error();

    if (auto applied = state.setNoteVelocity(clipId_, noteId_, velocity_); !applied)
        return applied.error();

    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"noteId", Value{noteId_.toString()}},
                          {"previousVelocity", Value{note.value().velocity}}});
}

Result<void> SetNoteVelocity::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = idAt<ClipId>(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteId = idAt<NoteId>(undoRecord, "noteId");
    if (!noteId)
        return noteId.error();

    auto previous = undoRecord.intAt("previousVelocity");
    if (!previous)
        return previous.error();

    return state.setNoteVelocity(clipId.value(), noteId.value(), static_cast<int>(previous.value()));
}

bool SetNoteVelocity::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetNoteVelocity*>(&newer);
    return other != nullptr && other->clipId_ == clipId_ && other->noteId_ == noteId_;
}

// ---------------------------------------------------------------------------
// note.quantize
// ---------------------------------------------------------------------------

QuantizeNotes::QuantizeNotes(ClipId clipId, std::vector<NoteId> noteIds, double gridBeats)
    : clipId_{clipId}
    , noteIds_{std::move(noteIds)}
    , gridBeats_{gridBeats}
{
}

Result<std::unique_ptr<Command>> QuantizeNotes::fromPayload(const Value& payload)
{
    auto clipId = idAt<ClipId>(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteIds = noteIdsAt(payload, "noteIds");
    if (!noteIds)
        return noteIds.error();

    auto gridBeats = payload.doubleAt("gridBeats");
    if (!gridBeats)
        return gridBeats.error();

    return std::unique_ptr<Command>{
        new QuantizeNotes{clipId.value(), std::move(noteIds.value()), gridBeats.value()}};
}

Value QuantizeNotes::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"noteIds", toValue(noteIds_)},
                          {"gridBeats", Value{gridBeats_}}});
}

Result<Value> QuantizeNotes::apply(ProjectState& state) const
{
    if (!(gridBeats_ > 0.0))
        return fail(ErrorCode::invalidArgument, "the grid must be longer than nothing");

    // Every note is read and checked before a single one moves: all-or-nothing
    // is what makes the undo record describe a state that really existed.
    std::vector<Note> before;
    before.reserve(noteIds_.size());
    for (const auto& noteId : noteIds_)
    {
        auto note = findNote(state, clipId_, noteId);
        if (!note)
            return note.error();
        before.push_back(note.value());
    }

    for (const auto& note : before)
    {
        const auto snapped = std::round(note.startBeats / gridBeats_) * gridBeats_;
        if (auto moved = state.moveNote(clipId_, note.id, note.pitch, snapped); !moved)
            return moved.error();
    }

    Value::Array previous;
    previous.reserve(before.size());
    for (const auto& note : before)
    {
        previous.push_back(
            Value::object({{"noteId", Value{note.id.toString()}}, {"startBeats", Value{note.startBeats}}}));
    }

    return Value::object(
        {{"clipId", Value{clipId_.toString()}}, {"previous", Value::array(std::move(previous))}});
}

Result<void> QuantizeNotes::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = idAt<ClipId>(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    const auto* found = undoRecord.find("previous");
    if (found == nullptr || found->asArray() == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: previous");

    for (const auto& item : *found->asArray())
    {
        auto noteId = idAt<NoteId>(item, "noteId");
        if (!noteId)
            return noteId.error();

        auto startBeats = item.doubleAt("startBeats");
        if (!startBeats)
            return startBeats.error();

        // The pitch is read from the clip rather than from the record: this
        // command never changed it, and writing back a pitch it did not touch
        // would undo a transposition that happened in between.
        auto note = findNote(state, clipId.value(), noteId.value());
        if (!note)
            return note.error();

        if (auto moved =
                state.moveNote(clipId.value(), noteId.value(), note.value().pitch, startBeats.value());
            !moved)
            return moved.error();
    }

    return {};
}

// ---------------------------------------------------------------------------
// note.transpose
// ---------------------------------------------------------------------------

TransposeNotes::TransposeNotes(ClipId clipId, std::vector<NoteId> noteIds, int semitones)
    : clipId_{clipId}
    , noteIds_{std::move(noteIds)}
    , semitones_{semitones}
{
}

Result<std::unique_ptr<Command>> TransposeNotes::fromPayload(const Value& payload)
{
    auto clipId = idAt<ClipId>(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteIds = noteIdsAt(payload, "noteIds");
    if (!noteIds)
        return noteIds.error();

    auto semitones = payload.intAt("semitones");
    if (!semitones)
        return semitones.error();

    return std::unique_ptr<Command>{
        new TransposeNotes{clipId.value(), std::move(noteIds.value()), static_cast<int>(semitones.value())}};
}

Value TransposeNotes::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"noteIds", toValue(noteIds_)},
                          {"semitones", Value{semitones_}}});
}

Result<Value> TransposeNotes::apply(ProjectState& state) const
{
    std::vector<Note> before;
    before.reserve(noteIds_.size());
    for (const auto& noteId : noteIds_)
    {
        auto note = findNote(state, clipId_, noteId);
        if (!note)
            return note.error();

        const auto pitch = note.value().pitch + semitones_;
        if (pitch < Note::lowestPitch || pitch > Note::highestPitch)
            return fail(ErrorCode::invalidArgument,
                        "transposing would take a note out of range: " + std::to_string(pitch));

        before.push_back(note.value());
    }

    for (const auto& note : before)
    {
        if (auto moved = state.moveNote(clipId_, note.id, note.pitch + semitones_, note.startBeats); !moved)
            return moved.error();
    }

    // The interval, not the pitches: undoing is transposing back, and a record
    // of absolute pitches would fight any edit made in between.
    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"noteIds", toValue(noteIds_)},
                          {"semitones", Value{semitones_}}});
}

Result<void> TransposeNotes::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = idAt<ClipId>(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    auto noteIds = noteIdsAt(undoRecord, "noteIds");
    if (!noteIds)
        return noteIds.error();

    auto semitones = undoRecord.intAt("semitones");
    if (!semitones)
        return semitones.error();

    for (const auto& noteId : noteIds.value())
    {
        auto note = findNote(state, clipId.value(), noteId);
        if (!note)
            return note.error();

        if (auto moved = state.moveNote(clipId.value(),
                                        noteId,
                                        note.value().pitch - static_cast<int>(semitones.value()),
                                        note.value().startBeats);
            !moved)
            return moved.error();
    }

    return {};
}

} // namespace daw::domain
