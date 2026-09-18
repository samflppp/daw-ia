#include "daw/domain/commands/SetTrackVolume.h"

namespace daw::domain
{

SetTrackVolume::SetTrackVolume(TrackId trackId, double volumeDb)
    : trackId_{trackId}
    , volumeDb_{volumeDb}
{
}

Result<std::unique_ptr<Command>> SetTrackVolume::fromPayload(const Value& payload)
{
    auto trackText = payload.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto volumeDb = payload.doubleAt("volumeDb");
    if (!volumeDb)
        return volumeDb.error();

    return std::unique_ptr<Command>{new SetTrackVolume{trackId.value(), volumeDb.value()}};
}

Value SetTrackVolume::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"volumeDb", Value{volumeDb_}}});
}

Result<Value> SetTrackVolume::apply(ProjectState& state) const
{
    auto previous = state.trackVolume(trackId_);
    if (!previous)
        return previous.error();

    auto applied = state.setTrackVolume(trackId_, volumeDb_);
    if (!applied)
        return applied.error();

    // The overwritten value: this is what only exists once the command has run,
    // and the reason undo records are separate from payloads.
    return Value::object(
        {{"trackId", Value{trackId_.toString()}}, {"previousVolumeDb", Value{previous.value()}}});
}

Result<void> SetTrackVolume::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackText = undoRecord.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto previous = undoRecord.doubleAt("previousVolumeDb");
    if (!previous)
        return previous.error();

    return state.setTrackVolume(trackId.value(), previous.value());
}

bool SetTrackVolume::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetTrackVolume*>(&newer);
    return other != nullptr && other->trackId_ == trackId_;
}

} // namespace daw::domain
