#include "daw/domain/commands/PatternCommands.h"

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

} // namespace

// ---------------------------------------------------------------------------
// pattern.create
// ---------------------------------------------------------------------------

CreatePattern::CreatePattern(PatternId patternId, std::string name, double lengthBeats)
    : patternId_{patternId}
    , name_{std::move(name)}
    , lengthBeats_{lengthBeats}
{
}

Result<std::unique_ptr<Command>> CreatePattern::fromPayload(const Value& payload)
{
    auto patternId = idAt<PatternId>(payload, "patternId");
    if (!patternId)
        return patternId.error();

    auto name = payload.stringAt("name");
    if (!name)
        return name.error();

    auto lengthBeats = payload.doubleAt("lengthBeats");
    if (!lengthBeats)
        return lengthBeats.error();

    return std::unique_ptr<Command>{
        new CreatePattern{patternId.value(), std::move(name).value(), lengthBeats.value()}};
}

Value CreatePattern::payload() const
{
    return Value::object({{"patternId", Value{patternId_.toString()}},
                          {"name", Value{name_}},
                          {"lengthBeats", Value{lengthBeats_}}});
}

Result<Value> CreatePattern::apply(ProjectState& state) const
{
    Pattern pattern{};
    pattern.id = patternId_;
    pattern.name = name_;
    pattern.lengthBeats = lengthBeats_;

    auto added = state.addPattern(std::move(pattern));
    if (!added)
        return added.error();

    return Value::object({{"patternId", Value{patternId_.toString()}}});
}

Result<void> CreatePattern::revert(ProjectState& state, const Value& undoRecord) const
{
    auto patternId = idAt<PatternId>(undoRecord, "patternId");
    if (!patternId)
        return patternId.error();

    return state.removePattern(patternId.value());
}

// ---------------------------------------------------------------------------
// pattern.place
// ---------------------------------------------------------------------------

PlacePattern::PlacePattern(PlacementId placementId, PatternId patternId, double startBeats)
    : placementId_{placementId}
    , patternId_{patternId}
    , startBeats_{startBeats}
{
}

Result<std::unique_ptr<Command>> PlacePattern::fromPayload(const Value& payload)
{
    auto placementId = idAt<PlacementId>(payload, "placementId");
    if (!placementId)
        return placementId.error();

    auto patternId = idAt<PatternId>(payload, "patternId");
    if (!patternId)
        return patternId.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    return std::unique_ptr<Command>{
        new PlacePattern{placementId.value(), patternId.value(), startBeats.value()}};
}

Value PlacePattern::payload() const
{
    return Value::object({{"placementId", Value{placementId_.toString()}},
                          {"patternId", Value{patternId_.toString()}},
                          {"startBeats", Value{startBeats_}}});
}

Result<Value> PlacePattern::apply(ProjectState& state) const
{
    Placement placement{};
    placement.id = placementId_;
    placement.patternId = patternId_;
    placement.startBeats = startBeats_;

    auto placed = state.addPlacement(placement);
    if (!placed)
        return placed.error();

    return Value::object({{"placementId", Value{placementId_.toString()}}});
}

Result<void> PlacePattern::revert(ProjectState& state, const Value& undoRecord) const
{
    auto placementId = idAt<PlacementId>(undoRecord, "placementId");
    if (!placementId)
        return placementId.error();

    return state.removePlacement(placementId.value());
}

// ---------------------------------------------------------------------------
// pattern.add_track
// ---------------------------------------------------------------------------

AddPatternTrack::AddPatternTrack(PatternId patternId, ClipId clipId, TrackId trackId)
    : patternId_{patternId}
    , clipId_{clipId}
    , trackId_{trackId}
{
}

Result<std::unique_ptr<Command>> AddPatternTrack::fromPayload(const Value& payload)
{
    auto patternId = idAt<PatternId>(payload, "patternId");
    if (!patternId)
        return patternId.error();

    auto clipId = idAt<ClipId>(payload, "clipId");
    if (!clipId)
        return clipId.error();

    auto trackId = idAt<TrackId>(payload, "trackId");
    if (!trackId)
        return trackId.error();

    return std::unique_ptr<Command>{new AddPatternTrack{patternId.value(), clipId.value(), trackId.value()}};
}

Value AddPatternTrack::payload() const
{
    return Value::object({{"patternId", Value{patternId_.toString()}},
                          {"clipId", Value{clipId_.toString()}},
                          {"trackId", Value{trackId_.toString()}}});
}

Result<Value> AddPatternTrack::apply(ProjectState& state) const
{
    Clip clip{};
    clip.id = clipId_;
    clip.trackId = trackId_;

    auto added = state.addClip(patternId_, std::move(clip));
    if (!added)
        return added.error();

    return Value::object({{"clipId", Value{clipId_.toString()}}});
}

Result<void> AddPatternTrack::revert(ProjectState& state, const Value& undoRecord) const
{
    auto clipId = idAt<ClipId>(undoRecord, "clipId");
    if (!clipId)
        return clipId.error();

    return state.removeClip(clipId.value());
}

// ---------------------------------------------------------------------------
// pattern.set_length
// ---------------------------------------------------------------------------

SetPatternLength::SetPatternLength(PatternId patternId, double lengthBeats)
    : patternId_{patternId}
    , lengthBeats_{lengthBeats}
{
}

Result<std::unique_ptr<Command>> SetPatternLength::fromPayload(const Value& payload)
{
    auto patternId = idAt<PatternId>(payload, "patternId");
    if (!patternId)
        return patternId.error();

    auto lengthBeats = payload.doubleAt("lengthBeats");
    if (!lengthBeats)
        return lengthBeats.error();

    return std::unique_ptr<Command>{new SetPatternLength{patternId.value(), lengthBeats.value()}};
}

Value SetPatternLength::payload() const
{
    return Value::object({{"patternId", Value{patternId_.toString()}}, {"lengthBeats", Value{lengthBeats_}}});
}

Result<Value> SetPatternLength::apply(ProjectState& state) const
{
    const auto* pattern = state.findPattern(patternId_);
    if (pattern == nullptr)
        return fail(ErrorCode::notFound, "no such pattern: " + patternId_.toString());

    const auto previous = pattern->lengthBeats;

    if (auto applied = state.setPatternLength(patternId_, lengthBeats_); !applied)
        return applied.error();

    return Value::object(
        {{"patternId", Value{patternId_.toString()}}, {"previousLengthBeats", Value{previous}}});
}

Result<void> SetPatternLength::revert(ProjectState& state, const Value& undoRecord) const
{
    auto patternId = idAt<PatternId>(undoRecord, "patternId");
    if (!patternId)
        return patternId.error();

    auto previous = undoRecord.doubleAt("previousLengthBeats");
    if (!previous)
        return previous.error();

    return state.setPatternLength(patternId.value(), previous.value());
}

bool SetPatternLength::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetPatternLength*>(&newer);
    return other != nullptr && other->patternId_ == patternId_;
}

} // namespace daw::domain
