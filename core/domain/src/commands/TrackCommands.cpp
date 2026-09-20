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

} // namespace daw::domain
