#include "daw/domain/project/ProjectState.h"

#include <algorithm>
#include <array>
#include <cstdint>
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
// TempoPoint
// ---------------------------------------------------------------------------

Result<void> TempoPoint::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "tempo point identifier is nil");
    if (startBeats < 0.0)
        return fail(ErrorCode::invalidArgument, "a tempo point cannot start before the timeline origin");
    if (beatsPerMinute < ProjectState::minTempo || beatsPerMinute > ProjectState::maxTempo)
        return fail(ErrorCode::invalidArgument, "tempo out of range: " + std::to_string(beatsPerMinute));
    return {};
}

Value TempoPoint::toValue() const
{
    return Value::object({{"id", Value{id.toString()}},
                          {"startBeats", Value{startBeats}},
                          {"beatsPerMinute", Value{beatsPerMinute}}});
}

Result<TempoPoint> TempoPoint::fromValue(const Value& value)
{
    auto id = idAt<TempoPointId>(value, "id");
    if (!id)
        return id.error();

    auto startBeats = value.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    auto beatsPerMinute = value.doubleAt("beatsPerMinute");
    if (!beatsPerMinute)
        return beatsPerMinute.error();

    TempoPoint point{};
    point.id = id.value();
    point.startBeats = startBeats.value();
    point.beatsPerMinute = beatsPerMinute.value();

    if (auto valid = point.validate(); !valid)
        return valid.error();

    return point;
}

bool operator==(const TempoPoint& lhs, const TempoPoint& rhs)
{
    return lhs.id == rhs.id && lhs.startBeats == rhs.startBeats && lhs.beatsPerMinute == rhs.beatsPerMinute;
}

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
// PluginRef
// ---------------------------------------------------------------------------

Result<void> PluginRef::validate() const
{
    if (format != PluginRef::vst3Format && format != PluginRef::clapFormat)
        return fail(ErrorCode::invalidArgument, "unsupported plugin format: " + format);
    if (identifier.empty())
        return fail(ErrorCode::invalidArgument, "plugin identifier is empty");
    return {};
}

Value PluginRef::toValue() const
{
    return Value::object(
        {{"format", Value{format}}, {"identifier", Value{identifier}}, {"name", Value{name}}});
}

Result<PluginRef> PluginRef::fromValue(const Value& value)
{
    auto format = value.stringAt("format");
    if (!format)
        return format.error();

    auto identifier = value.stringAt("identifier");
    if (!identifier)
        return identifier.error();

    auto name = value.stringAt("name");
    if (!name)
        return name.error();

    PluginRef ref{};
    ref.format = format.value();
    ref.identifier = identifier.value();
    ref.name = name.value();

    auto valid = ref.validate();
    if (!valid)
        return valid.error();

    return ref;
}

bool operator==(const PluginRef& lhs, const PluginRef& rhs)
{
    return lhs.format == rhs.format && lhs.identifier == rhs.identifier && lhs.name == rhs.name;
}

// ---------------------------------------------------------------------------
// PluginParam
// ---------------------------------------------------------------------------

Result<void> PluginParam::validate() const
{
    if (paramId.empty())
        return fail(ErrorCode::invalidArgument, "parameter identifier is empty");
    if (!(value >= 0.0 && value <= 1.0))
        return fail(ErrorCode::invalidArgument, "parameter out of the normalised range: " + paramId);
    return {};
}

Value PluginParam::toValue() const
{
    return Value::object({{"paramId", Value{paramId}}, {"value", Value{value}}});
}

Result<PluginParam> PluginParam::fromValue(const Value& value)
{
    auto paramId = value.stringAt("paramId");
    if (!paramId)
        return paramId.error();

    auto normalised = value.doubleAt("value");
    if (!normalised)
        return normalised.error();

    PluginParam param{};
    param.paramId = paramId.value();
    param.value = normalised.value();

    auto valid = param.validate();
    if (!valid)
        return valid.error();

    return param;
}

bool operator==(const PluginParam& lhs, const PluginParam& rhs)
{
    return lhs.paramId == rhs.paramId && lhs.value == rhs.value;
}

// ---------------------------------------------------------------------------
// PluginInstance
// ---------------------------------------------------------------------------

const PluginParam* PluginInstance::findParam(std::string_view paramId) const noexcept
{
    for (const auto& param : params)
    {
        if (param.paramId == paramId)
            return &param;
    }
    return nullptr;
}

Result<void> PluginInstance::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "plugin identifier is nil");

    auto validRef = ref.validate();
    if (!validRef)
        return validRef;

    auto validState = state.validate();
    if (!validState)
        return validState;

    // Sorted and without duplicates, so that two instances holding the same
    // parameters always serialise to the same text.
    for (std::size_t index = 0; index < params.size(); ++index)
    {
        auto valid = params[index].validate();
        if (!valid)
            return valid;

        if (index > 0 && !(params[index - 1].paramId < params[index].paramId))
            return fail(ErrorCode::invalidArgument,
                        "parameters are not sorted, or are duplicated: " + params[index].paramId);
    }
    return {};
}

Value PluginInstance::toValue() const
{
    Value::Array serialisedParams;
    serialisedParams.reserve(params.size());
    for (const auto& param : params)
        serialisedParams.push_back(param.toValue());

    return Value::object({{"id", Value{id.toString()}},
                          {"ref", ref.toValue()},
                          {"bypassed", Value{bypassed}},
                          {"params", Value::array(std::move(serialisedParams))},
                          {"state", state.toValue()}});
}

Result<PluginInstance> PluginInstance::fromValue(const Value& value)
{
    auto id = idAt<PluginId>(value, "id");
    if (!id)
        return id.error();

    const auto* refValue = value.find("ref");
    if (refValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: ref");

    auto ref = PluginRef::fromValue(*refValue);
    if (!ref)
        return ref.error();

    auto bypassed = value.boolAt("bypassed");
    if (!bypassed)
        return bypassed.error();

    PluginInstance plugin{};
    plugin.id = id.value();
    plugin.ref = ref.value();
    plugin.bypassed = bypassed.value();

    if (const auto* stateValue = value.find("state"); stateValue != nullptr)
    {
        auto state = StateBlobRef::fromValue(*stateValue);
        if (!state)
            return state.error();
        plugin.state = state.value();
    }

    if (const auto* paramsValue = value.find("params"); paramsValue != nullptr)
    {
        const auto* items = paramsValue->asArray();
        if (items == nullptr)
            return fail(ErrorCode::invalidPayload, "params must be an array");

        plugin.params.reserve(items->size());
        for (const auto& item : *items)
        {
            auto param = PluginParam::fromValue(item);
            if (!param)
                return param.error();
            plugin.params.push_back(param.value());
        }
    }

    auto valid = plugin.validate();
    if (!valid)
        return valid.error();

    return plugin;
}

bool operator==(const PluginInstance& lhs, const PluginInstance& rhs)
{
    return lhs.id == rhs.id && lhs.ref == rhs.ref && lhs.bypassed == rhs.bypassed &&
           lhs.params == rhs.params && lhs.state == rhs.state;
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
    if (pan < ProjectState::minPan || pan > ProjectState::maxPan)
        return fail(ErrorCode::invalidArgument, "pan out of range: " + std::to_string(pan));

    for (const auto& clip : clips)
    {
        auto valid = clip.validate();
        if (!valid)
            return valid;
    }

    for (const auto& plugin : plugins)
    {
        auto valid = plugin.validate();
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

    Value::Array serialisedPlugins;
    serialisedPlugins.reserve(plugins.size());
    for (const auto& plugin : plugins)
        serialisedPlugins.push_back(plugin.toValue());

    return Value::object({{"id", Value{id.toString()}},
                          {"name", Value{name}},
                          {"volumeDb", Value{volumeDb}},
                          {"muted", Value{muted}},
                          {"pan", Value{pan}},
                          {"clips", Value::array(std::move(serialisedClips))},
                          {"plugins", Value::array(std::move(serialisedPlugins))}});
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

    // Absent means "not muted": every project written before mute existed says
    // exactly that, and a missing key is not a malformed track.
    if (value.find("muted") != nullptr)
    {
        auto muted = value.boolAt("muted");
        if (!muted)
            return muted.error();
        track.muted = muted.value();
    }

    // Same reading for pan: absent means centred, which is where every project
    // written before pan existed actually sat.
    if (value.find("pan") != nullptr)
    {
        auto pan = value.doubleAt("pan");
        if (!pan)
            return pan.error();
        track.pan = pan.value();
    }

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

    if (const auto* pluginsValue = value.find("plugins"); pluginsValue != nullptr)
    {
        const auto* items = pluginsValue->asArray();
        if (items == nullptr)
            return fail(ErrorCode::invalidPayload, "plugins must be an array");

        track.plugins.reserve(items->size());
        for (const auto& item : *items)
        {
            auto plugin = PluginInstance::fromValue(item);
            if (!plugin)
                return plugin.error();
            track.plugins.push_back(plugin.value());
        }
    }

    auto valid = track.validate();
    if (!valid)
        return valid.error();

    return track;
}

bool operator==(const Track& lhs, const Track& rhs)
{
    return lhs.id == rhs.id && lhs.name == rhs.name && lhs.volumeDb == rhs.volumeDb && lhs.pan == rhs.pan &&
           lhs.muted == rhs.muted && lhs.clips == rhs.clips && lhs.plugins == rhs.plugins;
}

// ---------------------------------------------------------------------------
// ProjectState
// ---------------------------------------------------------------------------

TempoPointId ProjectState::originTempoPointId() noexcept
{
    // Not nil, because a nil identifier is how every other entity says "not
    // set". The value itself carries no meaning beyond being the same in every
    // project, on every machine, forever.
    std::array<std::uint8_t, 16> bytes{};
    bytes[15] = 1;
    return TempoPointId{Ulid{bytes}};
}

const TempoPoint* ProjectState::findTempoPoint(TempoPointId id) const noexcept
{
    for (const auto& point : tempo_)
    {
        if (point.id == id)
            return &point;
    }
    return nullptr;
}

TempoPoint* ProjectState::findTempoPointMutable(TempoPointId id) noexcept
{
    for (auto& point : tempo_)
    {
        if (point.id == id)
            return &point;
    }
    return nullptr;
}

Result<TempoPoint> ProjectState::tempoPoint(TempoPointId id) const
{
    const auto* point = findTempoPoint(id);
    if (point == nullptr)
        return fail(ErrorCode::notFound, "no tempo point " + id.toString());
    return *point;
}

void ProjectState::sortTempoPoints()
{
    std::stable_sort(tempo_.begin(),
                     tempo_.end(),
                     [](const TempoPoint& lhs, const TempoPoint& rhs)
                     { return lhs.startBeats < rhs.startBeats; });
}

double ProjectState::tempoAt(double beats) const noexcept
{
    // The sequence is sorted and starts at beat 0, so the first point always
    // answers and the loop needs no fallback of its own. A tempo is held until
    // the next point: there is nothing to interpolate.
    double beatsPerMinute = tempo_.front().beatsPerMinute;
    for (const auto& point : tempo_)
    {
        if (point.startBeats > beats)
            break;
        beatsPerMinute = point.beatsPerMinute;
    }
    return beatsPerMinute;
}

Result<void> ProjectState::insertTempoPoint(TempoPoint point)
{
    if (auto valid = point.validate(); !valid)
        return valid.error();

    if (findTempoPoint(point.id) != nullptr)
        return fail(ErrorCode::conflict, "tempo point already exists: " + point.id.toString());

    // Two points on the same beat would make tempoAt() depend on the order they
    // were inserted in, which a replay has no reason to reproduce.
    for (const auto& existing : tempo_)
    {
        if (existing.startBeats == point.startBeats)
            return fail(ErrorCode::conflict,
                        "a tempo point already starts at beat " + std::to_string(point.startBeats));
    }

    tempo_.push_back(point);
    sortTempoPoints();
    return {};
}

Result<void> ProjectState::removeTempoPoint(TempoPointId id)
{
    if (id == originTempoPointId())
        return fail(ErrorCode::invalidArgument, "the tempo point at the origin cannot be removed");

    const auto found =
        std::find_if(tempo_.begin(), tempo_.end(), [id](const TempoPoint& point) { return point.id == id; });
    if (found == tempo_.end())
        return fail(ErrorCode::notFound, "no tempo point " + id.toString());

    tempo_.erase(found);
    return {};
}

Result<void> ProjectState::setTempoPointBpm(TempoPointId id, double beatsPerMinute)
{
    if (beatsPerMinute < minTempo || beatsPerMinute > maxTempo)
        return fail(ErrorCode::invalidArgument, "tempo out of range: " + std::to_string(beatsPerMinute));

    auto* point = findTempoPointMutable(id);
    if (point == nullptr)
        return fail(ErrorCode::notFound, "no tempo point " + id.toString());

    point->beatsPerMinute = beatsPerMinute;
    return {};
}

Result<void> ProjectState::moveTempoPoint(TempoPointId id, double startBeats)
{
    if (id == originTempoPointId())
        return fail(ErrorCode::invalidArgument, "the tempo point at the origin cannot be moved");

    if (!(startBeats > 0.0))
        return fail(ErrorCode::invalidArgument, "only the point at the origin can start at beat 0");

    auto* point = findTempoPointMutable(id);
    if (point == nullptr)
        return fail(ErrorCode::notFound, "no tempo point " + id.toString());

    for (const auto& existing : tempo_)
    {
        if (existing.id != id && existing.startBeats == startBeats)
            return fail(ErrorCode::conflict,
                        "a tempo point already starts at beat " + std::to_string(startBeats));
    }

    point->startBeats = startBeats;
    sortTempoPoints();
    return {};
}

Result<void> ProjectState::readTempoSequence(const Value::Array& points)
{
    // Read into a state that already holds its origin point: the sequence on
    // disk holds it too, so the origin is updated and the others are added.
    // That keeps one single invariant instead of two code paths.
    bool sawOrigin = false;

    for (const auto& item : points)
    {
        auto point = TempoPoint::fromValue(item);
        if (!point)
            return point.error();

        if (point.value().id == originTempoPointId())
        {
            if (point.value().startBeats != 0.0)
                return fail(ErrorCode::invalidPayload, "the tempo point at the origin must start at beat 0");

            tempo_.front() = point.value();
            sawOrigin = true;
            continue;
        }

        auto inserted = insertTempoPoint(point.value());
        if (!inserted)
            return inserted.error();
    }

    if (!sawOrigin)
        return fail(ErrorCode::invalidPayload, "the tempo sequence has no point at the origin");

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

Result<std::size_t> ProjectState::trackIndex(TrackId id) const
{
    const auto position =
        std::find_if(tracks_.begin(), tracks_.end(), [id](const Track& track) { return track.id == id; });
    if (position == tracks_.end())
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    return static_cast<std::size_t>(std::distance(tracks_.begin(), position));
}

Result<void> ProjectState::insertTrack(Track track, std::size_t index)
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

    const auto position = std::min(index, tracks_.size());
    tracks_.insert(tracks_.begin() + static_cast<std::ptrdiff_t>(position), std::move(track));
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

Result<double> ProjectState::trackPan(TrackId id) const
{
    const auto* track = findTrack(id);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    return track->pan;
}

Result<void> ProjectState::setTrackPan(TrackId id, double pan)
{
    if (pan < minPan || pan > maxPan)
        return fail(ErrorCode::invalidArgument, "pan out of range: " + std::to_string(pan));

    auto* track = findTrackMutable(id);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    track->pan = pan;
    return {};
}

Result<bool> ProjectState::trackMuted(TrackId id) const
{
    const auto* track = findTrack(id);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    return track->muted;
}

Result<void> ProjectState::setTrackMuted(TrackId id, bool muted)
{
    auto* track = findTrackMutable(id);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + id.toString());

    track->muted = muted;
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

Result<std::size_t> ProjectState::noteIndex(ClipId clipId, NoteId noteId) const
{
    const auto* clip = findClip(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId.toString());

    for (std::size_t index = 0; index < clip->notes.size(); ++index)
    {
        if (clip->notes[index].id == noteId)
            return index;
    }

    return fail(ErrorCode::notFound, "no such note: " + noteId.toString());
}

Result<void> ProjectState::insertNote(ClipId clipId, Note note, std::size_t index)
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

    // Beyond the current count it appends, like insertPlugin and insertTrack: a
    // replayed payload must not fail on a clip that grew differently.
    const auto at = std::min(index, clip->notes.size());
    clip->notes.insert(clip->notes.begin() + static_cast<std::ptrdiff_t>(at), note);
    return {};
}

Result<void> ProjectState::moveNote(ClipId clipId, NoteId noteId, int pitch, double startBeats)
{
    auto* clip = findClipMutable(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId.toString());

    const auto position = std::find_if(
        clip->notes.begin(), clip->notes.end(), [noteId](const Note& note) { return note.id == noteId; });
    if (position == clip->notes.end())
        return fail(ErrorCode::notFound, "no such note: " + noteId.toString());

    // Validated before anything is written, on a copy: a refused move leaves
    // the note exactly as it was, which is what the bus relies on to promise
    // that a failed command creates no history entry.
    Note moved = *position;
    moved.pitch = pitch;
    moved.startBeats = startBeats;

    auto valid = moved.validate();
    if (!valid)
        return valid;

    *position = moved;
    return {};
}

Result<void> ProjectState::resizeNote(ClipId clipId, NoteId noteId, double lengthBeats)
{
    auto* clip = findClipMutable(clipId);
    if (clip == nullptr)
        return fail(ErrorCode::notFound, "no such clip: " + clipId.toString());

    const auto position = std::find_if(
        clip->notes.begin(), clip->notes.end(), [noteId](const Note& note) { return note.id == noteId; });
    if (position == clip->notes.end())
        return fail(ErrorCode::notFound, "no such note: " + noteId.toString());

    // Validated on a copy before anything is written, like moveNote: a length
    // of zero or of NaN must leave the note as it was.
    Note resized = *position;
    resized.lengthBeats = lengthBeats;

    auto valid = resized.validate();
    if (!valid)
        return valid;

    *position = resized;
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

// --- plugins ---------------------------------------------------------------

const PluginInstance* ProjectState::findPlugin(PluginId id) const noexcept
{
    for (const auto& track : tracks_)
    {
        for (const auto& plugin : track.plugins)
        {
            if (plugin.id == id)
                return &plugin;
        }
    }
    return nullptr;
}

PluginInstance* ProjectState::findPluginMutable(PluginId id) noexcept
{
    for (auto& track : tracks_)
    {
        for (auto& plugin : track.plugins)
        {
            if (plugin.id == id)
                return &plugin;
        }
    }
    return nullptr;
}

Result<ProjectState::PluginLocation> ProjectState::pluginLocation(PluginId id) const
{
    for (const auto& track : tracks_)
    {
        for (std::size_t index = 0; index < track.plugins.size(); ++index)
        {
            if (track.plugins[index].id == id)
                return PluginLocation{track.id, index};
        }
    }
    return fail(ErrorCode::notFound, "no such plugin: " + id.toString());
}

Result<void> ProjectState::insertPlugin(TrackId trackId, PluginInstance plugin, std::size_t index)
{
    auto valid = plugin.validate();
    if (!valid)
        return valid;

    auto* track = findTrackMutable(trackId);
    if (track == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId.toString());

    if (findPlugin(plugin.id) != nullptr)
        return fail(ErrorCode::conflict, "plugin already exists: " + plugin.id.toString());

    // Past the end means "at the end". A replayed payload must not fail just
    // because the chain is shorter than it was when the command was recorded.
    const auto position = std::min(index, track->plugins.size());
    track->plugins.insert(track->plugins.begin() + static_cast<std::ptrdiff_t>(position), std::move(plugin));
    return {};
}

Result<void> ProjectState::removePlugin(PluginId id)
{
    for (auto& track : tracks_)
    {
        const auto position = std::find_if(track.plugins.begin(),
                                           track.plugins.end(),
                                           [id](const PluginInstance& plugin) { return plugin.id == id; });
        if (position != track.plugins.end())
        {
            track.plugins.erase(position);
            return {};
        }
    }
    return fail(ErrorCode::notFound, "no such plugin: " + id.toString());
}

Result<void> ProjectState::setPluginBypassed(PluginId id, bool bypassed)
{
    auto* plugin = findPluginMutable(id);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + id.toString());

    plugin->bypassed = bypassed;
    return {};
}

Result<void> ProjectState::setPluginParameter(PluginId id, std::string paramId, double value)
{
    PluginParam param{};
    param.paramId = std::move(paramId);
    param.value = value;

    auto valid = param.validate();
    if (!valid)
        return valid;

    auto* plugin = findPluginMutable(id);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + id.toString());

    const auto position = std::lower_bound(plugin->params.begin(),
                                           plugin->params.end(),
                                           param.paramId,
                                           [](const PluginParam& entry, const std::string& wanted)
                                           { return entry.paramId < wanted; });

    if (position != plugin->params.end() && position->paramId == param.paramId)
        position->value = param.value;
    else
        plugin->params.insert(position, std::move(param));

    return {};
}

Result<void> ProjectState::clearPluginParameter(PluginId id, std::string_view paramId)
{
    auto* plugin = findPluginMutable(id);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + id.toString());

    const auto position =
        std::find_if(plugin->params.begin(),
                     plugin->params.end(),
                     [paramId](const PluginParam& entry) { return entry.paramId == paramId; });
    if (position == plugin->params.end())
        return fail(ErrorCode::notFound, "no such parameter: " + std::string{paramId});

    plugin->params.erase(position);
    return {};
}

Result<void> ProjectState::setPluginState(PluginId id, StateBlobRef state)
{
    auto valid = state.validate();
    if (!valid)
        return valid;

    auto* plugin = findPluginMutable(id);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + id.toString());

    plugin->state = std::move(state);
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

    Value::Array serialisedTempo;
    serialisedTempo.reserve(tempo_.size());
    for (const auto& point : tempo_)
        serialisedTempo.push_back(point.toValue());

    return Value::object({{"tempo", Value::array(std::move(serialisedTempo))},
                          {"tracks", Value::array(std::move(serialisedTracks))}});
}

Result<ProjectState> ProjectState::fromValue(const Value& value)
{
    const auto* tempoValue = value.find("tempo");
    if (tempoValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: tempo");

    ProjectState state;

    if (const auto* points = tempoValue->asArray(); points != nullptr)
    {
        auto read = state.readTempoSequence(*points);
        if (!read)
            return read.error();
    }
    else
    {
        // A project written before the sequence existed carries a single
        // number. It is worth a sequence of one point at the origin, which is
        // exactly what an empty project already holds, so only the value
        // moves. Nothing already on disk is rewritten: no journal has ever
        // held a tempo command, which is the whole reason this is cheap today.
        auto scalar = tempoValue->asDouble();
        if (!scalar)
            return fail(scalar.error().code, "tempo: " + scalar.error().message);

        auto applied = state.setTempoPointBpm(originTempoPointId(), scalar.value());
        if (!applied)
            return applied.error();
    }

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
