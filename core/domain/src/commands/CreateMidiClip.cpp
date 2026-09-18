#include "daw/domain/commands/CreateMidiClip.h"

namespace daw::domain
{

CreateMidiClip::CreateMidiClip(TrackId trackId, ClipId clipId, double startBeats, double lengthBeats)
    : trackId_{trackId}
    , clipId_{clipId}
    , startBeats_{startBeats}
    , lengthBeats_{lengthBeats}
{
}

Result<std::unique_ptr<Command>> CreateMidiClip::fromPayload(const Value& payload)
{
    auto trackText = payload.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto clipText = payload.stringAt("clipId");
    if (!clipText)
        return clipText.error();

    auto clipId = ClipId::parse(clipText.value());
    if (!clipId)
        return fail(clipId.error().code, "clipId: " + clipId.error().message);

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    auto lengthBeats = payload.doubleAt("lengthBeats");
    if (!lengthBeats)
        return lengthBeats.error();

    return std::unique_ptr<Command>{
        new CreateMidiClip{trackId.value(), clipId.value(), startBeats.value(), lengthBeats.value()}};
}

Value CreateMidiClip::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}},
                          {"clipId", Value{clipId_.toString()}},
                          {"startBeats", Value{startBeats_}},
                          {"lengthBeats", Value{lengthBeats_}}});
}

Result<Value> CreateMidiClip::apply(ProjectState& state) const
{
    Clip clip{};
    clip.id = clipId_;
    clip.startBeats = startBeats_;
    clip.lengthBeats = lengthBeats_;

    auto added = state.addClip(trackId_, std::move(clip));
    if (!added)
        return added.error();

    // Undoing a creation only needs to know what to remove.
    return Value::object({{"clipId", Value{clipId_.toString()}}});
}

Result<void> CreateMidiClip::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipText = undoRecord.stringAt("clipId");
    if (!clipText)
        return clipText.error();

    auto clipId = ClipId::parse(clipText.value());
    if (!clipId)
        return fail(clipId.error().code, "clipId: " + clipId.error().message);

    return state.removeClip(clipId.value());
}

} // namespace daw::domain
