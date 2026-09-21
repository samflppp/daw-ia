#include "daw/domain/commands/TrackCommands.h"

#include <cstdint>
#include <utility>

namespace daw::domain
{
namespace
{

Result<TrackId> trackIdAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto parsed = TrackId::parse(text.value());
    if (!parsed)
        return fail(parsed.error().code, std::string{key} + ": " + parsed.error().message);

    return parsed.value();
}

} // namespace

// ---------------------------------------------------------------------------
// AddTrack
// ---------------------------------------------------------------------------

AddTrack::AddTrack(TrackId trackId, std::string name, double volumeDb)
    : trackId_{trackId}
    , name_{std::move(name)}
    , volumeDb_{volumeDb}
{
}

Result<std::unique_ptr<Command>> AddTrack::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto name = payload.stringAt("name");
    if (!name)
        return name.error();

    auto volumeDb = payload.doubleAt("volumeDb");
    if (!volumeDb)
        return volumeDb.error();

    return std::unique_ptr<Command>{new AddTrack{trackId.value(), std::move(name).value(), volumeDb.value()}};
}

Value AddTrack::payload() const
{
    return Value::object(
        {{"trackId", Value{trackId_.toString()}}, {"name", Value{name_}}, {"volumeDb", Value{volumeDb_}}});
}

Result<Value> AddTrack::apply(ProjectState& state) const
{
    Track track{};
    track.id = trackId_;
    track.name = name_;
    track.volumeDb = volumeDb_;

    auto added = state.addTrack(std::move(track));
    if (!added)
        return added.error();

    return Value::object({{"trackId", Value{trackId_.toString()}}});
}

Result<void> AddTrack::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    return state.removeTrack(trackId.value());
}

// ---------------------------------------------------------------------------
// RemoveTrack
// ---------------------------------------------------------------------------

RemoveTrack::RemoveTrack(TrackId trackId)
    : trackId_{trackId}
{
}

Result<std::unique_ptr<Command>> RemoveTrack::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    return std::unique_ptr<Command>{new RemoveTrack{trackId.value()}};
}

Value RemoveTrack::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}});
}

Result<Value> RemoveTrack::apply(ProjectState& state) const
{
    auto index = state.trackIndex(trackId_);
    if (!index)
        return index.error();

    const auto* track = state.findTrack(trackId_);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId_.toString());

    // The whole track, clips and plugins included: nothing else can put back
    // what the removal is about to drop.
    auto record = Value::object(
        {{"index", Value{static_cast<std::int64_t>(index.value())}}, {"track", track->toValue()}});

    auto removed = state.removeTrack(trackId_);
    if (!removed)
        return removed.error();

    return record;
}

Result<void> RemoveTrack::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* trackValue = undoRecord.find("track");
    if (trackValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: track");

    auto track = Track::fromValue(*trackValue);
    if (!track)
        return track.error();

    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();

    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    return state.insertTrack(std::move(track).value(), static_cast<std::size_t>(index.value()));
}

SetTrackMuted::SetTrackMuted(TrackId trackId, bool muted)
    : trackId_{trackId}
    , muted_{muted}
{
}

Result<std::unique_ptr<Command>> SetTrackMuted::fromPayload(const Value& payload)
{
    auto trackText = payload.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto muted = payload.boolAt("muted");
    if (!muted)
        return muted.error();

    return std::unique_ptr<Command>{new SetTrackMuted{trackId.value(), muted.value()}};
}

Value SetTrackMuted::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"muted", Value{muted_}}});
}

Result<Value> SetTrackMuted::apply(ProjectState& state) const
{
    auto previous = state.trackMuted(trackId_);
    if (!previous)
        return previous.error();

    auto applied = state.setTrackMuted(trackId_, muted_);
    if (!applied)
        return applied.error();

    return Value::object(
        {{"trackId", Value{trackId_.toString()}}, {"previousMuted", Value{previous.value()}}});
}

Result<void> SetTrackMuted::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackText = undoRecord.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto previous = undoRecord.boolAt("previousMuted");
    if (!previous)
        return previous.error();

    return state.setTrackMuted(trackId.value(), previous.value());
}

// ---------------------------------------------------------------------------
// track.set_pan
// ---------------------------------------------------------------------------

SetTrackPan::SetTrackPan(TrackId trackId, double pan)
    : trackId_{trackId}
    , pan_{pan}
{
}

Result<std::unique_ptr<Command>> SetTrackPan::fromPayload(const Value& payload)
{
    auto trackText = payload.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto pan = payload.doubleAt("pan");
    if (!pan)
        return pan.error();

    return std::unique_ptr<Command>{new SetTrackPan{trackId.value(), pan.value()}};
}

Value SetTrackPan::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"pan", Value{pan_}}});
}

Result<Value> SetTrackPan::apply(ProjectState& state) const
{
    auto previous = state.trackPan(trackId_);
    if (!previous)
        return previous.error();

    if (auto applied = state.setTrackPan(trackId_, pan_); !applied)
        return applied.error();

    return Value::object({{"trackId", Value{trackId_.toString()}}, {"previousPan", Value{previous.value()}}});
}

Result<void> SetTrackPan::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackText = undoRecord.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto previous = undoRecord.doubleAt("previousPan");
    if (!previous)
        return previous.error();

    return state.setTrackPan(trackId.value(), previous.value());
}

bool SetTrackPan::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetTrackPan*>(&newer);
    return other != nullptr && other->trackId_ == trackId_;
}

} // namespace daw::domain
