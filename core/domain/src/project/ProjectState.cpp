#include "daw/domain/project/ProjectState.h"

#include <algorithm>
#include <string>

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

Result<void> requireFinitePositive(double length, std::string_view what)
{
    if (!(length > 0.0))
        return fail(ErrorCode::invalidArgument, std::string{what} + " must be greater than zero");
    return {};
}

} // namespace

// ---------------------------------------------------------------------------
// Note
// ---------------------------------------------------------------------------

Result<void> Note::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "note identifier is nil");
    if (pitch < lowestPitch || pitch > highestPitch)
        return fail(ErrorCode::invalidArgument, "pitch out of range: " + std::to_string(pitch));
    if (velocity < lowestVelocity || velocity > highestVelocity)
        return fail(ErrorCode::invalidArgument, "velocity out of range: " + std::to_string(velocity));
    if (startBeats < 0.0)
        return fail(ErrorCode::invalidArgument, "note starts before the clip");
    return requireFinitePositive(lengthBeats, "note length");
}

Value Note::toValue() const
{
    return Value::object({{"id", Value{id.toString()}},
                          {"pitch", Value{pitch}},
                          {"velocity", Value{velocity}},
                          {"startBeats", Value{startBeats}},
                          {"lengthBeats", Value{lengthBeats}}});
}

Result<Note> Note::fromValue(const Value& value)
{
    auto id = idAt<NoteId>(value, "id");
    if (!id)
        return id.error();

    auto pitch = value.intAt("pitch");
    if (!pitch)
        return pitch.error();

    auto velocity = value.intAt("velocity");
    if (!velocity)
        return velocity.error();

    auto startBeats = value.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    auto lengthBeats = value.doubleAt("lengthBeats");
    if (!lengthBeats)
        return lengthBeats.error();

    Note note{};
    note.id = id.value();
    note.pitch = static_cast<int>(pitch.value());
    note.velocity = static_cast<int>(velocity.value());
    note.startBeats = startBeats.value();
    note.lengthBeats = lengthBeats.value();

    auto valid = note.validate();
    if (!valid)
        return valid.error();

    return note;
}

bool operator==(const Note& lhs, const Note& rhs)
{
    return lhs.id == rhs.id && lhs.pitch == rhs.pitch && lhs.velocity == rhs.velocity &&
           lhs.startBeats == rhs.startBeats && lhs.lengthBeats == rhs.lengthBeats;
}

// ---------------------------------------------------------------------------
// Clip
// ---------------------------------------------------------------------------

Result<void> Clip::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "clip identifier is nil");
    if (startBeats < 0.0)
        return fail(ErrorCode::invalidArgument, "clip starts before the timeline origin");

    auto length = requireFinitePositive(lengthBeats, "clip length");
    if (!length)
        return length;

    for (const auto& note : notes)
    {
        auto valid = note.validate();
        if (!valid)
            return valid;
    }
    return {};
}

Value Clip::toValue() const
{
    Value::Array serialisedNotes;
    serialisedNotes.reserve(notes.size());
    for (const auto& note : notes)
        serialisedNotes.push_back(note.toValue());

    return Value::object({{"id", Value{id.toString()}},
                          {"startBeats", Value{startBeats}},
                          {"lengthBeats", Value{lengthBeats}},
                          {"notes", Value::array(std::move(serialisedNotes))}});
}

Result<Clip> Clip::fromValue(const Value& value)
{
    auto id = idAt<ClipId>(value, "id");
    if (!id)
        return id.error();

    auto startBeats = value.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    auto lengthBeats = value.doubleAt("lengthBeats");
    if (!lengthBeats)
        return lengthBeats.error();

    Clip clip{};
    clip.id = id.value();
    clip.startBeats = startBeats.value();
    clip.lengthBeats = lengthBeats.value();

    const auto* notesValue = value.find("notes");
    if (notesValue != nullptr)
    {
        const auto* items = notesValue->asArray();
        if (items == nullptr)
            return fail(ErrorCode::invalidPayload, "notes must be an array");

        clip.notes.reserve(items->size());
        for (const auto& item : *items)
        {
            auto note = Note::fromValue(item);
            if (!note)
                return note.error();
            clip.notes.push_back(note.value());
        }
    }

    auto valid = clip.validate();
    if (!valid)
        return valid.error();

    return clip;
}

bool operator==(const Clip& lhs, const Clip& rhs)
{
    return lhs.id == rhs.id && lhs.startBeats == rhs.startBeats && lhs.lengthBeats == rhs.lengthBeats &&
           lhs.notes == rhs.notes;
}

// ---------------------------------------------------------------------------
// Track
// ---------------------------------------------------------------------------

Result<void> Track::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "track identifier is nil");
    if (volumeDb < ProjectState::minVolumeDb || volumeDb > ProjectState::maxVolumeDb)
        return fail(ErrorCode::invalidArgument, "volume out of range: " + std::to_string(volumeDb));

    for (const auto& clip : clips)
    {
        auto valid = clip.validate();
        if (!valid)
            return valid;
    }
    return {};
}

Value Track::toValue() const
{
    Value::Array serialisedClips;
    serialisedClips.reserve(clips.size());
    for (const auto& clip : clips)
        serialisedClips.push_back(clip.toValue());

    return Value::object({{"id", Value{id.toString()}},
                          {"name", Value{name}},
                          {"volumeDb", Value{volumeDb}},
                          {"clips", Value::array(std::move(serialisedClips))}});
}

Result<Track> Track::fromValue(const Value& value)
{
    auto id = idAt<TrackId>(value, "id");
    if (!id)
        return id.error();

    auto name = value.stringAt("name");
    if (!name)
        return name.error();

    auto volumeDb = value.doubleAt("volumeDb");
    if (!volumeDb)
        return volumeDb.error();

    Track track{};
    track.id = id.value();
    track.name = name.value();
    track.volumeDb = volumeDb.value();

    const auto* clipsValue = value.find("clips");
    if (clipsValue != nullptr)
    {
        const auto* items = clipsValue->asArray();
        if (items == nullptr)
            return fail(ErrorCode::invalidPayload, "clips must be an array");

        track.clips.reserve(items->size());
        for (const auto& item : *items)
        {
            auto clip = Clip::fromValue(item);
            if (!clip)
                return clip.error();
            track.clips.push_back(clip.value());
        }
    }

    auto valid = track.validate();
    if (!valid)
        return valid.error();

    return track;
}

bool operator==(const Track& lhs, const Track& rhs)
{
    return lhs.id == rhs.id && lhs.name == rhs.name && lhs.volumeDb == rhs.volumeDb && lhs.clips == rhs.clips;
}

// ---------------------------------------------------------------------------
// ProjectState
// ---------------------------------------------------------------------------

Result<void> ProjectState::setTempo(double beatsPerMinute)
{
    if (beatsPerMinute < minTempo || beatsPerMinute > maxTempo)
        return fail(ErrorCode::invalidArgument, "tempo out of range: " + std::to_string(beatsPerMinute));

    tempo_ = beatsPerMinute;
    return {};
}

const Track* ProjectState::findTrack(TrackId id) const noexcept
{
    for (const auto& track : tracks_)
    {
        if (track.id == id)
            return &track;
    }
    return nullptr;
}

Track* ProjectState::findTrackMutable(TrackId id) noexcept
{
    for (auto& track : tracks_)
    {
        if (track.id == id)
            return &track;
    }
    return nullptr;
}

const Clip* ProjectState::findClip(ClipId id) const noexcept
{
    for (const auto& track : tracks_)
    {
        for (const auto& clip : track.clips)
        {
            if (clip.id == id)
                return &clip;
        }
    }
    return nullptr;
}

Clip* ProjectState::findClipMutable(ClipId id) noexcept
{
    for (auto& track : tracks_)
    {
        for (auto& clip : track.clips)
        {
            if (clip.id == id)
                return &clip;
        }
    }
    return nullptr;
}

Result<void> ProjectState::addTrack(Track track)
{
    auto valid = track.validate();
    if (!valid)
        return valid;

    if (findTrack(track.id) != nullptr)
        return fail(ErrorCode::conflict, "track already exists: " + track.id.toString());

    for (const auto& clip : track.clips)
    {
        if (findClip(clip.id) != nullptr)
            return fail(ErrorCode::conflict, "clip already exists: " + clip.id.toString());
    }

    tracks_.push_back(std::move(track));
    return {};
}

Result<void> ProjectState::removeTrack(TrackId id)
{
    const auto position =
        std::find_if(tracks_.begin(), tracks_.end(), [id](const Track& track) { return track.id == id; });
    if (position == tracks_.end())
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    tracks_.erase(position);
    return {};
}

Result<double> ProjectState::trackVolume(TrackId id) const
{
    const auto* track = findTrack(id);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    return track->volumeDb;
}

Result<void> ProjectState::setTrackVolume(TrackId id, double volumeDb)
{
    if (volumeDb < minVolumeDb || volumeDb > maxVolumeDb)
        return fail(ErrorCode::invalidArgument, "volume out of range: " + std::to_string(volumeDb));

    auto* track = findTrackMutable(id);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    track->volumeDb = volumeDb;
    return {};
}

Result<void> ProjectState::addClip(TrackId trackId, Clip clip)
{
    auto valid = clip.validate();
    if (!valid)
        return valid;

    auto* track = findTrackMutable(trackId);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId.toString());

    if (findClip(clip.id) != nullptr)
        return fail(ErrorCode::conflict, "clip already exists: " + clip.id.toString());

    track->clips.push_back(std::move(clip));
    return {};
}

Result<void> ProjectState::removeClip(ClipId id)
{
    for (auto& track : tracks_)
    {
        const auto position = std::find_if(
            track.clips.begin(), track.clips.end(), [id](const Clip& clip) { return clip.id == id; });
        if (position != track.clips.end())
        {
            track.clips.erase(position);
            return {};
        }
    }
    return fail(ErrorCode::notFound, "no such clip: " + id.toString());
}

Result<void> ProjectState::addNote(ClipId clipId, Note note)
{
    auto valid = note.validate();
    if (!valid)
        return valid;

    auto* clip = findClipMutable(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId.toString());

    const auto existing = std::find_if(
        clip->notes.begin(), clip->notes.end(), [&note](const Note& other) { return other.id == note.id; });
    if (existing != clip->notes.end())
        return fail(ErrorCode::conflict, "note already exists: " + note.id.toString());

    clip->notes.push_back(note);
    return {};
}

Result<void> ProjectState::removeNote(ClipId clipId, NoteId noteId)
{
    auto* clip = findClipMutable(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId.toString());

    const auto position = std::find_if(
        clip->notes.begin(), clip->notes.end(), [noteId](const Note& note) { return note.id == noteId; });
    if (position == clip->notes.end())
        return fail(ErrorCode::notFound, "no such note: " + noteId.toString());

    clip->notes.erase(position);
    return {};
}

Result<void> ProjectState::setPlaying(bool playing)
{
    transport_.playing = playing;
    return {};
}

Result<void> ProjectState::setPositionBeats(double positionBeats)
{
    if (positionBeats < 0.0)
        return fail(ErrorCode::invalidArgument, "the playhead cannot go before the timeline origin");

    transport_.positionBeats = positionBeats;
    return {};
}

Value ProjectState::toValue() const
{
    Value::Array serialisedTracks;
    serialisedTracks.reserve(tracks_.size());
    for (const auto& track : tracks_)
        serialisedTracks.push_back(track.toValue());

    return Value::object({{"tempo", Value{tempo_}}, {"tracks", Value::array(std::move(serialisedTracks))}});
}

Result<ProjectState> ProjectState::fromValue(const Value& value)
{
    auto tempo = value.doubleAt("tempo");
    if (!tempo)
        return tempo.error();

    ProjectState state;
    auto applied = state.setTempo(tempo.value());
    if (!applied)
        return applied.error();

    const auto* tracksValue = value.find("tracks");
    if (tracksValue != nullptr)
    {
        const auto* items = tracksValue->asArray();
        if (items == nullptr)
            return fail(ErrorCode::invalidPayload, "tracks must be an array");

        for (const auto& item : *items)
        {
            auto track = Track::fromValue(item);
            if (!track)
                return track.error();

            auto added = state.addTrack(track.value());
            if (!added)
                return added.error();
        }
    }

    return state;
}

bool operator==(const ProjectState& lhs, const ProjectState& rhs)
{
    return lhs.tempo_ == rhs.tempo_ && lhs.tracks_ == rhs.tracks_;
}

} // namespace daw::domain
