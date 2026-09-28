#include "daw/domain/commands/PatternCommands.h"

#include <cstdint>
#include <utility>
#include <vector>

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

// A line is optional in every payload that can carry one: absent is what a
// payload written before S17 says, and it reads as nil.
Result<LaneId> optionalLaneAt(const Value& value)
{
    if (value.find("laneId") == nullptr)
        return LaneId{};
    return idAt<LaneId>(value, "laneId");
}

// Whether an undo record says a line was created; absent means no.
bool flagAt(const Value& record, std::string_view key)
{
    const auto* value = record.find(key);
    if (value == nullptr)
        return false;
    auto flag = value->asBool();
    return flag.ok() && flag.value();
}

} // namespace

// ---------------------------------------------------------------------------
// pattern.create
// ---------------------------------------------------------------------------

CreatePattern::CreatePattern(PatternId patternId, std::string name, double lengthBeats, bool ownLane)
    : patternId_{patternId}
    , name_{std::move(name)}
    , lengthBeats_{lengthBeats}
    , ownLane_{ownLane}
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

    auto ownLane = true;
    if (const auto* value = payload.find("ownLane"); value != nullptr)
    {
        auto flag = value->asBool();
        if (!flag)
            return fail(flag.error().code, "ownLane: " + flag.error().message);
        ownLane = flag.value();
    }

    return std::unique_ptr<Command>{
        new CreatePattern{patternId.value(), std::move(name).value(), lengthBeats.value(), ownLane}};
}

Value CreatePattern::payload() const
{
    Value::Object members{{"patternId", Value{patternId_.toString()}},
                          {"name", Value{name_}},
                          {"lengthBeats", Value{lengthBeats_}}};

    // Only when false: a pattern with its own line serialises as before S17.
    if (!ownLane_)
        members.emplace_back("ownLane", Value{false});
    return Value::object(std::move(members));
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

    auto laneCreated = false;
    if (ownLane_)
    {
        auto ensured = state.ensureLaneOfPattern(patternId_);
        if (!ensured)
            return ensured.error();
        laneCreated = ensured.value();
    }

    return Value::object({{"patternId", Value{patternId_.toString()}}, {"laneCreated", Value{laneCreated}}});
}

Result<void> CreatePattern::revert(ProjectState& state, const Value& undoRecord) const
{
    auto patternId = idAt<PatternId>(undoRecord, "patternId");
    if (!patternId)
        return patternId.error();

    if (auto removed = state.removePattern(patternId.value()); !removed)
        return removed;

    if (flagAt(undoRecord, "laneCreated"))
        return state.removeLane(ProjectState::laneOfPattern(patternId.value()));
    return {};
}

// ---------------------------------------------------------------------------
// pattern.place
// ---------------------------------------------------------------------------

PlacePattern::PlacePattern(PlacementId placementId, PatternId patternId, double startBeats, LaneId laneId)
    : placementId_{placementId}
    , patternId_{patternId}
    , startBeats_{startBeats}
    , laneId_{laneId}
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

    auto laneId = optionalLaneAt(payload);
    if (!laneId)
        return laneId.error();

    return std::unique_ptr<Command>{
        new PlacePattern{placementId.value(), patternId.value(), startBeats.value(), laneId.value()}};
}

Value PlacePattern::payload() const
{
    Value::Object members{{"placementId", Value{placementId_.toString()}},
                          {"patternId", Value{patternId_.toString()}},
                          {"startBeats", Value{startBeats_}}};
    if (!laneId_.isNil())
        members.emplace_back("laneId", Value{laneId_.toString()});
    return Value::object(std::move(members));
}

Result<Value> PlacePattern::apply(ProjectState& state) const
{
    Placement placement{};
    placement.id = placementId_;
    placement.patternId = patternId_;
    placement.startBeats = startBeats_;
    placement.laneId = laneId_;

    // Without a line, the pattern's own, which may have to be created — and
    // then taken away again by the undo.
    auto laneCreated = false;
    if (laneId_.isNil())
    {
        if (state.findPattern(patternId_) == nullptr)
            return fail(ErrorCode::notFound, "no such pattern: " + patternId_.toString());
        auto ensured = state.ensureLaneOfPattern(patternId_);
        if (!ensured)
            return ensured.error();
        laneCreated = ensured.value();
        placement.laneId = ProjectState::laneOfPattern(patternId_);
    }

    auto placed = state.insertPlacement(placement, state.arrangement().size());
    if (!placed)
    {
        if (laneCreated)
            static_cast<void>(state.removeLane(placement.laneId));
        return placed.error();
    }

    return Value::object({{"placementId", Value{placementId_.toString()}},
                          {"laneId", Value{placement.laneId.toString()}},
                          {"laneCreated", Value{laneCreated}}});
}

Result<void> PlacePattern::revert(ProjectState& state, const Value& undoRecord) const
{
    auto placementId = idAt<PlacementId>(undoRecord, "placementId");
    if (!placementId)
        return placementId.error();

    if (auto removed = state.removePlacement(placementId.value()); !removed)
        return removed;

    if (flagAt(undoRecord, "laneCreated"))
    {
        auto laneId = idAt<LaneId>(undoRecord, "laneId");
        if (!laneId)
            return laneId.error();
        return state.removeLane(laneId.value());
    }
    return {};
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

// ---------------------------------------------------------------------------
// pattern.rename
// ---------------------------------------------------------------------------

RenamePattern::RenamePattern(PatternId patternId, std::string name)
    : patternId_{patternId}
    , name_{std::move(name)}
{
}

Result<std::unique_ptr<Command>> RenamePattern::fromPayload(const Value& payload)
{
    auto patternId = idAt<PatternId>(payload, "patternId");
    if (!patternId)
        return patternId.error();

    auto name = payload.stringAt("name");
    if (!name)
        return name.error();

    return std::unique_ptr<Command>{new RenamePattern{patternId.value(), std::move(name).value()}};
}

Value RenamePattern::payload() const
{
    return Value::object({{"patternId", Value{patternId_.toString()}}, {"name", Value{name_}}});
}

Result<Value> RenamePattern::apply(ProjectState& state) const
{
    const auto* pattern = state.findPattern(patternId_);
    if (pattern == nullptr)
        return fail(ErrorCode::notFound, "no such pattern: " + patternId_.toString());

    auto previous = pattern->name;

    if (auto applied = state.setPatternName(patternId_, name_); !applied)
        return applied.error();

    return Value::object({{"patternId", Value{patternId_.toString()}}, {"previousName", Value{previous}}});
}

Result<void> RenamePattern::revert(ProjectState& state, const Value& undoRecord) const
{
    auto patternId = idAt<PatternId>(undoRecord, "patternId");
    if (!patternId)
        return patternId.error();

    auto previous = undoRecord.stringAt("previousName");
    if (!previous)
        return previous.error();

    return state.setPatternName(patternId.value(), std::move(previous).value());
}

// ---------------------------------------------------------------------------
// pattern.remove
// ---------------------------------------------------------------------------

RemovePattern::RemovePattern(PatternId patternId)
    : patternId_{patternId}
{
}

Result<std::unique_ptr<Command>> RemovePattern::fromPayload(const Value& payload)
{
    auto patternId = idAt<PatternId>(payload, "patternId");
    if (!patternId)
        return patternId.error();

    return std::unique_ptr<Command>{new RemovePattern{patternId.value()}};
}

Value RemovePattern::payload() const
{
    return Value::object({{"patternId", Value{patternId_.toString()}}});
}

Result<Value> RemovePattern::apply(ProjectState& state) const
{
    const auto* pattern = state.findPattern(patternId_);
    if (pattern == nullptr)
        return fail(ErrorCode::notFound, "no such pattern: " + patternId_.toString());

    auto index = state.patternIndex(patternId_);
    if (!index)
        return index.error();

    // Each placement with its rank in the arrangement, read before anything
    // moves. They are listed in arrangement order, which is the order revert
    // reinserts them in: inserting by ascending rank puts each one exactly
    // where it was.
    Value::Array placements;
    for (std::size_t rank = 0; rank < state.arrangement().size(); ++rank)
    {
        const auto& placement = state.arrangement()[rank];
        if (placement.patternId != patternId_)
            continue;

        placements.push_back(Value::object(
            {{"placement", placement.toValue()}, {"index", Value{static_cast<std::int64_t>(rank)}}}));
    }

    Value::Object members{{"pattern", pattern->toValue()},
                          {"index", Value{static_cast<std::int64_t>(index.value())}},
                          {"placements", Value::array(std::move(placements))}};

    if (auto removed = state.removePattern(patternId_); !removed)
        return removed.error();

    // Its own line, if it is left empty and nobody named it.
    const auto ownLane = ProjectState::laneOfPattern(patternId_);
    if (const auto* lane = state.findLane(ownLane);
        lane != nullptr && lane->name.empty() && state.laneIsEmpty(ownLane))
    {
        auto laneRank = state.laneIndex(ownLane);
        if (!laneRank)
            return laneRank.error();
        members.emplace_back("lane", lane->toValue());
        members.emplace_back("laneIndex", Value{static_cast<std::int64_t>(laneRank.value())});
        if (auto dropped = state.removeLane(ownLane); !dropped)
            return dropped.error();
    }

    return Value::object(std::move(members));
}

Result<void> RemovePattern::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* patternValue = undoRecord.find("pattern");
    if (patternValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: pattern");

    auto pattern = Pattern::fromValue(*patternValue);
    if (!pattern)
        return pattern.error();

    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();
    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    const auto* placementsValue = undoRecord.find("placements");
    const auto* placements = placementsValue != nullptr ? placementsValue->asArray() : nullptr;
    if (placements == nullptr)
        return fail(ErrorCode::invalidPayload, "placements must be an array");

    // Everything is parsed before anything is written, so a damaged record
    // leaves the state as it found it.
    std::vector<std::pair<Placement, std::size_t>> restored;
    restored.reserve(placements->size());
    for (const auto& entry : *placements)
    {
        const auto* placementValue = entry.find("placement");
        if (placementValue == nullptr)
            return fail(ErrorCode::invalidPayload, "missing key: placement");

        auto placement = Placement::fromValue(*placementValue);
        if (!placement)
            return placement.error();

        auto rank = entry.intAt("index");
        if (!rank)
            return rank.error();
        if (rank.value() < 0)
            return fail(ErrorCode::invalidPayload, "index is negative");

        restored.emplace_back(placement.value(), static_cast<std::size_t>(rank.value()));
    }

    // The line first: the placements name it.
    if (const auto* laneValue = undoRecord.find("lane"); laneValue != nullptr)
    {
        auto lane = Lane::fromValue(*laneValue);
        if (!lane)
            return lane.error();
        auto laneRank = undoRecord.intAt("laneIndex");
        if (!laneRank)
            return laneRank.error();
        if (laneRank.value() < 0)
            return fail(ErrorCode::invalidPayload, "laneIndex is negative");
        if (auto laneBack =
                state.insertLane(std::move(lane).value(), static_cast<std::size_t>(laneRank.value()));
            !laneBack)
            return laneBack;
    }

    auto inserted = state.insertPattern(std::move(pattern).value(), static_cast<std::size_t>(index.value()));
    if (!inserted)
        return inserted;

    for (const auto& [placement, rank] : restored)
    {
        if (auto placed = state.insertPlacement(placement, rank); !placed)
            return placed;
    }

    return {};
}

// ---------------------------------------------------------------------------
// placement.move
// ---------------------------------------------------------------------------

MovePlacement::MovePlacement(PlacementId placementId, double startBeats, LaneId laneId)
    : placementId_{placementId}
    , startBeats_{startBeats}
    , laneId_{laneId}
{
}

Result<std::unique_ptr<Command>> MovePlacement::fromPayload(const Value& payload)
{
    auto placementId = idAt<PlacementId>(payload, "placementId");
    if (!placementId)
        return placementId.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    auto laneId = optionalLaneAt(payload);
    if (!laneId)
        return laneId.error();

    return std::unique_ptr<Command>{
        new MovePlacement{placementId.value(), startBeats.value(), laneId.value()}};
}

Value MovePlacement::payload() const
{
    Value::Object members{{"placementId", Value{placementId_.toString()}},
                          {"startBeats", Value{startBeats_}}};
    if (!laneId_.isNil())
        members.emplace_back("laneId", Value{laneId_.toString()});
    return Value::object(std::move(members));
}

Result<Value> MovePlacement::apply(ProjectState& state) const
{
    const auto* placement = state.findPlacement(placementId_);
    if (placement == nullptr)
        return fail(ErrorCode::notFound, "no such placement: " + placementId_.toString());

    const auto previous = placement->startBeats;
    const auto previousLane = placement->laneId;

    if (auto moved = state.movePlacement(placementId_, startBeats_, laneId_); !moved)
        return moved.error();

    return Value::object({{"placementId", Value{placementId_.toString()}},
                          {"previousStartBeats", Value{previous}},
                          {"previousLaneId", Value{previousLane.toString()}}});
}

Result<void> MovePlacement::revert(ProjectState& state, const Value& undoRecord) const
{
    auto placementId = idAt<PlacementId>(undoRecord, "placementId");
    if (!placementId)
        return placementId.error();

    auto previous = undoRecord.doubleAt("previousStartBeats");
    if (!previous)
        return previous.error();

    LaneId previousLane{};
    if (undoRecord.find("previousLaneId") != nullptr)
    {
        auto lane = idAt<LaneId>(undoRecord, "previousLaneId");
        if (!lane)
            return lane.error();
        previousLane = lane.value();
    }

    return state.movePlacement(placementId.value(), previous.value(), previousLane);
}

bool MovePlacement::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const MovePlacement*>(&newer);
    return other != nullptr && other->placementId_ == placementId_;
}

// ---------------------------------------------------------------------------
// placement.remove
// ---------------------------------------------------------------------------

RemovePlacement::RemovePlacement(PlacementId placementId)
    : placementId_{placementId}
{
}

Result<std::unique_ptr<Command>> RemovePlacement::fromPayload(const Value& payload)
{
    auto placementId = idAt<PlacementId>(payload, "placementId");
    if (!placementId)
        return placementId.error();

    return std::unique_ptr<Command>{new RemovePlacement{placementId.value()}};
}

Value RemovePlacement::payload() const
{
    return Value::object({{"placementId", Value{placementId_.toString()}}});
}

Result<Value> RemovePlacement::apply(ProjectState& state) const
{
    const auto* placement = state.findPlacement(placementId_);
    if (placement == nullptr)
        return fail(ErrorCode::notFound, "no such placement: " + placementId_.toString());

    auto index = state.placementIndex(placementId_);
    if (!index)
        return index.error();

    auto record = Value::object(
        {{"placement", placement->toValue()}, {"index", Value{static_cast<std::int64_t>(index.value())}}});

    if (auto removed = state.removePlacement(placementId_); !removed)
        return removed.error();

    return record;
}

Result<void> RemovePlacement::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* placementValue = undoRecord.find("placement");
    if (placementValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: placement");

    auto placement = Placement::fromValue(*placementValue);
    if (!placement)
        return placement.error();

    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();
    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    return state.insertPlacement(placement.value(), static_cast<std::size_t>(index.value()));
}

} // namespace daw::domain
