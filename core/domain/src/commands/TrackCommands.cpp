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

    // Every row this track plays, and which pattern holds it. The track itself
    // no longer carries its content — a row belongs to a pattern — so the
    // record has to name both, or an undo would put back a silent track.
    Value::Array rows;
    for (const auto& pattern : state.patterns())
    {
        const auto* clip = pattern.findClipForTrack(trackId_);
        if (clip == nullptr)
            continue;

        rows.push_back(
            Value::object({{"patternId", Value{pattern.id.toString()}}, {"clip", clip->toValue()}}));
    }

    // The whole track, plugins included, plus its rows: nothing else can put
    // back what the removal is about to drop.
    auto record = Value::object({{"index", Value{static_cast<std::int64_t>(index.value())}},
                                 {"track", track->toValue()},
                                 {"rows", Value::array(std::move(rows))}});

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

    const auto trackId = track.value().id;

    auto inserted = state.insertTrack(std::move(track).value(), static_cast<std::size_t>(index.value()));
    if (!inserted)
        return inserted;

    // A record written before patterns existed keeps the clips inside the
    // track, each with its own start and length. It means what it has always
    // meant: one pattern of one row per clip, laid where the clip started.
    auto legacy = legacyClipsOf(*trackValue);
    if (!legacy)
        return legacy.error();

    for (auto& clip : legacy.value())
    {
        auto rebuilt = state.addSingleTrackPattern(
            trackId, clip.id, clip.startBeats, clip.lengthBeats, std::move(clip.notes));
        if (!rebuilt)
            return rebuilt;
    }

    const auto* rowsValue = undoRecord.find("rows");
    if (rowsValue == nullptr)
        return {};

    const auto* rows = rowsValue->asArray();
    if (rows == nullptr)
        return fail(ErrorCode::invalidPayload, "rows must be an array");

    for (const auto& row : *rows)
    {
        auto patternText = row.stringAt("patternId");
        if (!patternText)
            return patternText.error();

        auto patternId = PatternId::parse(patternText.value());
        if (!patternId)
            return fail(patternId.error().code, "patternId: " + patternId.error().message);

        const auto* clipValue = row.find("clip");
        if (clipValue == nullptr)
            return fail(ErrorCode::invalidPayload, "missing key: clip");

        auto clip = Clip::fromValue(*clipValue);
        if (!clip)
            return clip.error();

        auto added = state.addClip(patternId.value(), std::move(clip).value());
        if (!added)
            return added;
    }

    return {};
}

SetTrackMuted::SetTrackMuted(TrackId trackId, bool muted)
    : trackId_{trackId}
    , muted_{muted}
{
}

Result<std::unique_ptr<Command>> SetTrackMuted::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

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
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

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
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

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
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

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

// ---------------------------------------------------------------------------
// track.rename
// ---------------------------------------------------------------------------

RenameTrack::RenameTrack(TrackId trackId, std::string name)
    : trackId_{trackId}
    , name_{std::move(name)}
{
}

Result<std::unique_ptr<Command>> RenameTrack::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto name = payload.stringAt("name");
    if (!name)
        return name.error();

    return std::unique_ptr<Command>{new RenameTrack{trackId.value(), name.value()}};
}

Value RenameTrack::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"name", Value{name_}}});
}

Result<Value> RenameTrack::apply(ProjectState& state) const
{
    const auto* track = state.findTrack(trackId_);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId_.toString());

    const auto previous = track->name;

    if (auto applied = state.setTrackName(trackId_, name_); !applied)
        return applied.error();

    return Value::object({{"trackId", Value{trackId_.toString()}}, {"previousName", Value{previous}}});
}

Result<void> RenameTrack::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto previous = undoRecord.stringAt("previousName");
    if (!previous)
        return previous.error();

    return state.setTrackName(trackId.value(), previous.value());
}

// ---------------------------------------------------------------------------
// track.reorder
// ---------------------------------------------------------------------------

ReorderTrack::ReorderTrack(TrackId trackId, std::size_t index)
    : trackId_{trackId}
    , index_{index}
{
}

Result<std::unique_ptr<Command>> ReorderTrack::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto index = payload.intAt("index");
    if (!index)
        return index.error();

    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index cannot be negative");

    return std::unique_ptr<Command>{
        new ReorderTrack{trackId.value(), static_cast<std::size_t>(index.value())}};
}

Value ReorderTrack::payload() const
{
    return Value::object(
        {{"trackId", Value{trackId_.toString()}}, {"index", Value{static_cast<std::int64_t>(index_)}}});
}

Result<Value> ReorderTrack::apply(ProjectState& state) const
{
    // Read before moving: the index the track is leaving is what the undo has
    // to put it back at, and it is gone once the move has run.
    auto previous = state.trackIndex(trackId_);
    if (!previous)
        return previous.error();

    if (auto moved = state.moveTrack(trackId_, index_); !moved)
        return moved.error();

    return Value::object({{"trackId", Value{trackId_.toString()}},
                          {"previousIndex", Value{static_cast<std::int64_t>(previous.value())}}});
}

Result<void> ReorderTrack::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto previous = undoRecord.intAt("previousIndex");
    if (!previous)
        return previous.error();

    if (previous.value() < 0)
        return fail(ErrorCode::invalidPayload, "index cannot be negative");

    return state.moveTrack(trackId.value(), static_cast<std::size_t>(previous.value()));
}

bool ReorderTrack::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const ReorderTrack*>(&newer);
    return other != nullptr && other->trackId_ == trackId_;
}

// ---------------------------------------------------------------------------
// track.set_channel_pitch
// ---------------------------------------------------------------------------

SetTrackChannelPitch::SetTrackChannelPitch(TrackId trackId, int pitch)
    : trackId_{trackId}
    , pitch_{pitch}
{
}

Result<std::unique_ptr<Command>> SetTrackChannelPitch::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto pitch = payload.intAt("pitch");
    if (!pitch)
        return pitch.error();

    return std::unique_ptr<Command>{
        new SetTrackChannelPitch{trackId.value(), static_cast<int>(pitch.value())}};
}

Value SetTrackChannelPitch::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"pitch", Value{pitch_}}});
}

Result<Value> SetTrackChannelPitch::apply(ProjectState& state) const
{
    auto previous = state.trackChannelPitch(trackId_);
    if (!previous)
        return previous.error();

    if (auto applied = state.setTrackChannelPitch(trackId_, pitch_); !applied)
        return applied.error();

    return Value::object(
        {{"trackId", Value{trackId_.toString()}}, {"previousPitch", Value{previous.value()}}});
}

Result<void> SetTrackChannelPitch::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto previous = undoRecord.intAt("previousPitch");
    if (!previous)
        return previous.error();

    return state.setTrackChannelPitch(trackId.value(), static_cast<int>(previous.value()));
}

bool SetTrackChannelPitch::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetTrackChannelPitch*>(&newer);
    return other != nullptr && other->trackId_ == trackId_;
}

} // namespace daw::domain
