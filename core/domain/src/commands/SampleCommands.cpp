#include "daw/domain/commands/SampleCommands.h"

#include <algorithm>
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

// A sample, or null: the one payload of this file where "nothing" is a value.
Result<std::optional<SampleRef>> optionalSampleAt(const Value& value, std::string_view key)
{
    const auto* found = value.find(key);
    if (found == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: " + std::string{key});

    if (found->isNull())
        return std::optional<SampleRef>{};

    auto sample = SampleRef::fromValue(*found);
    if (!sample)
        return sample.error();

    return std::optional<SampleRef>{std::move(sample).value()};
}

Value optionalSampleValue(const std::optional<SampleRef>& sample)
{
    return sample.has_value() ? sample->toValue() : Value{};
}

} // namespace

// ---------------------------------------------------------------------------
// track.set_sample
// ---------------------------------------------------------------------------

SetTrackSample::SetTrackSample(TrackId trackId, std::optional<SampleRef> sample)
    : trackId_{trackId}
    , sample_{std::move(sample)}
{
}

Result<std::unique_ptr<Command>> SetTrackSample::fromPayload(const Value& payload)
{
    auto trackId = idAt<TrackId>(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto sample = optionalSampleAt(payload, "sample");
    if (!sample)
        return sample.error();

    return std::unique_ptr<Command>{new SetTrackSample{trackId.value(), std::move(sample).value()}};
}

Value SetTrackSample::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"sample", optionalSampleValue(sample_)}});
}

Result<Value> SetTrackSample::apply(ProjectState& state) const
{
    const auto* track = state.findTrack(trackId_);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId_.toString());

    auto previous = optionalSampleValue(track->sample);

    if (auto applied = state.setTrackSample(trackId_, sample_); !applied)
        return applied.error();

    return Value::object({{"trackId", Value{trackId_.toString()}}, {"previousSample", std::move(previous)}});
}

Result<void> SetTrackSample::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = idAt<TrackId>(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto previous = optionalSampleAt(undoRecord, "previousSample");
    if (!previous)
        return previous.error();

    return state.setTrackSample(trackId.value(), std::move(previous).value());
}

// ---------------------------------------------------------------------------
// audio.place
// ---------------------------------------------------------------------------

PlaceAudio::PlaceAudio(AudioClipId clipId, TrackId trackId, SampleRef sample, double startBeats)
    : clipId_{clipId}
    , trackId_{trackId}
    , sample_{std::move(sample)}
    , startBeats_{startBeats}
{
}

Result<std::unique_ptr<Command>> PlaceAudio::fromPayload(const Value& payload)
{
    auto clipId = idAt<AudioClipId>(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto trackId = idAt<TrackId>(payload, "trackId");
    if (!trackId)
        return trackId.error();

    const auto* sampleValue = payload.find("sample");
    if (sampleValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: sample");

    auto sample = SampleRef::fromValue(*sampleValue);
    if (!sample)
        return sample.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    return std::unique_ptr<Command>{
        new PlaceAudio{clipId.value(), trackId.value(), std::move(sample).value(), startBeats.value()}};
}

Value PlaceAudio::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}},
                          {"trackId", Value{trackId_.toString()}},
                          {"sample", sample_.toValue()},
                          {"startBeats", Value{startBeats_}}});
}

Result<Value> PlaceAudio::apply(ProjectState& state) const
{
    AudioClip clip{};
    clip.id = clipId_;
    clip.trackId = trackId_;
    clip.sample = sample_;
    clip.startBeats = startBeats_;

    if (auto added = state.addAudioClip(std::move(clip)); !added)
        return added.error();

    return Value::object({{"clipId", Value{clipId_.toString()}}});
}

Result<void> PlaceAudio::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = idAt<AudioClipId>(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    return state.removeAudioClip(clipId.value());
}

// ---------------------------------------------------------------------------
// audio.move
// ---------------------------------------------------------------------------

MoveAudio::MoveAudio(AudioClipId clipId, double startBeats)
    : clipId_{clipId}
    , startBeats_{startBeats}
{
}

Result<std::unique_ptr<Command>> MoveAudio::fromPayload(const Value& payload)
{
    auto clipId = idAt<AudioClipId>(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    return std::unique_ptr<Command>{new MoveAudio{clipId.value(), startBeats.value()}};
}

Value MoveAudio::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}}, {"startBeats", Value{startBeats_}}});
}

Result<Value> MoveAudio::apply(ProjectState& state) const
{
    const auto* clip = state.findAudioClip(clipId_);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such audio clip: " + clipId_.toString());

    const auto previous = clip->startBeats;

    if (auto moved = state.moveAudioClip(clipId_, startBeats_); !moved)
        return moved.error();

    return Value::object({{"clipId", Value{clipId_.toString()}}, {"previousStartBeats", Value{previous}}});
}

Result<void> MoveAudio::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = idAt<AudioClipId>(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    auto previous = undoRecord.doubleAt("previousStartBeats");
    if (!previous)
        return previous.error();

    return state.moveAudioClip(clipId.value(), previous.value());
}

bool MoveAudio::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const MoveAudio*>(&newer);
    return other != nullptr && other->clipId_ == clipId_;
}

// ---------------------------------------------------------------------------
// audio.remove
// ---------------------------------------------------------------------------

RemoveAudio::RemoveAudio(AudioClipId clipId)
    : clipId_{clipId}
{
}

Result<std::unique_ptr<Command>> RemoveAudio::fromPayload(const Value& payload)
{
    auto clipId = idAt<AudioClipId>(payload, "clipId");
    if (!clipId)
        return clipId.error();

    return std::unique_ptr<Command>{new RemoveAudio{clipId.value()}};
}

Value RemoveAudio::payload() const
{
    return Value::object({{"clipId", Value{clipId_.toString()}}});
}

Result<Value> RemoveAudio::apply(ProjectState& state) const
{
    const auto* clip = state.findAudioClip(clipId_);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such audio clip: " + clipId_.toString());

    auto index = state.audioClipIndex(clipId_);
    if (!index)
        return index.error();

    auto record = Value::object(
        {{"clip", clip->toValue()}, {"index", Value{static_cast<std::int64_t>(index.value())}}});

    if (auto removed = state.removeAudioClip(clipId_); !removed)
        return removed.error();

    return record;
}

Result<void> RemoveAudio::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* clipValue = undoRecord.find("clip");
    if (clipValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: clip");

    auto clip = AudioClip::fromValue(*clipValue);
    if (!clip)
        return clip.error();

    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();

    return state.insertAudioClip(std::move(clip).value(),
                                 static_cast<std::size_t>(std::max<std::int64_t>(0, index.value())));
}

} // namespace daw::domain
