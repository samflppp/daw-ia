#include "daw/domain/commands/LaneCommands.h"

#include <algorithm>
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

Result<std::size_t> indexAt(const Value& value, std::string_view key)
{
    auto index = value.intAt(key);
    if (!index)
        return index.error();
    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, std::string{key} + " is negative");
    return static_cast<std::size_t>(index.value());
}

Value indexValue(std::size_t index)
{
    return Value{static_cast<std::int64_t>(index)};
}

} // namespace

// ---------------------------------------------------------------------------
// lane.create
// ---------------------------------------------------------------------------

CreateLane::CreateLane(LaneId laneId, std::string name, std::size_t index)
    : laneId_{laneId}
    , name_{std::move(name)}
    , index_{index}
{
}

Result<std::unique_ptr<Command>> CreateLane::fromPayload(const Value& payload)
{
    auto laneId = idAt<LaneId>(payload, "laneId");
    if (!laneId)
        return laneId.error();

    auto name = payload.stringAt("name");
    if (!name)
        return name.error();

    auto index = indexAt(payload, "index");
    if (!index)
        return index.error();

    return std::unique_ptr<Command>{new CreateLane{laneId.value(), std::move(name).value(), index.value()}};
}

Value CreateLane::payload() const
{
    return Value::object(
        {{"laneId", Value{laneId_.toString()}}, {"name", Value{name_}}, {"index", indexValue(index_)}});
}

Result<Value> CreateLane::apply(ProjectState& state) const
{
    Lane lane{};
    lane.id = laneId_;
    lane.name = name_;

    if (auto inserted = state.insertLane(std::move(lane), index_); !inserted)
        return inserted.error();

    return Value::object({{"laneId", Value{laneId_.toString()}}});
}

Result<void> CreateLane::revert(ProjectState& state, const Value& undoRecord) const
{
    auto laneId = idAt<LaneId>(undoRecord, "laneId");
    if (!laneId)
        return laneId.error();

    return state.removeLane(laneId.value());
}

// ---------------------------------------------------------------------------
// lane.remove
// ---------------------------------------------------------------------------

RemoveLane::RemoveLane(LaneId laneId)
    : laneId_{laneId}
{
}

Result<std::unique_ptr<Command>> RemoveLane::fromPayload(const Value& payload)
{
    auto laneId = idAt<LaneId>(payload, "laneId");
    if (!laneId)
        return laneId.error();

    return std::unique_ptr<Command>{new RemoveLane{laneId.value()}};
}

Value RemoveLane::payload() const
{
    return Value::object({{"laneId", Value{laneId_.toString()}}});
}

Result<Value> RemoveLane::apply(ProjectState& state) const
{
    const auto* lane = state.findLane(laneId_);
    if (lane == nullptr)
        return fail(ErrorCode::notFound, "no such lane: " + laneId_.toString());

    auto index = state.laneIndex(laneId_);
    if (!index)
        return index.error();

    Value::Array placements;
    for (std::size_t rank = 0; rank < state.arrangement().size(); ++rank)
    {
        const auto& placement = state.arrangement()[rank];
        if (placement.laneId == laneId_)
            placements.push_back(
                Value::object({{"placement", placement.toValue()}, {"index", indexValue(rank)}}));
    }

    Value::Array audio;
    for (std::size_t rank = 0; rank < state.audioClips().size(); ++rank)
    {
        const auto& clip = state.audioClips()[rank];
        if (clip.laneId == laneId_)
            audio.push_back(Value::object({{"clip", clip.toValue()}, {"index", indexValue(rank)}}));
    }

    auto record = Value::object({{"lane", lane->toValue()},
                                 {"index", indexValue(index.value())},
                                 {"placements", Value::array(std::move(placements))},
                                 {"audio", Value::array(std::move(audio))}});

    if (auto removed = state.removeLane(laneId_); !removed)
        return removed.error();

    return record;
}

Result<void> RemoveLane::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* laneValue = undoRecord.find("lane");
    if (laneValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: lane");

    auto lane = Lane::fromValue(*laneValue);
    if (!lane)
        return lane.error();

    auto index = indexAt(undoRecord, "index");
    if (!index)
        return index.error();

    const auto* placementsValue = undoRecord.find("placements");
    const auto* placements = placementsValue != nullptr ? placementsValue->asArray() : nullptr;
    const auto* audioValue = undoRecord.find("audio");
    const auto* audio = audioValue != nullptr ? audioValue->asArray() : nullptr;
    if (placements == nullptr || audio == nullptr)
        return fail(ErrorCode::invalidPayload, "placements and audio must be arrays");

    // Everything is parsed before anything is written, so a damaged record
    // leaves the state as it found it.
    std::vector<std::pair<Placement, std::size_t>> restoredPlacements;
    for (const auto& entry : *placements)
    {
        const auto* value = entry.find("placement");
        if (value == nullptr)
            return fail(ErrorCode::invalidPayload, "missing key: placement");
        auto placement = Placement::fromValue(*value);
        if (!placement)
            return placement.error();
        auto rank = indexAt(entry, "index");
        if (!rank)
            return rank.error();
        restoredPlacements.emplace_back(placement.value(), rank.value());
    }

    std::vector<std::pair<AudioClip, std::size_t>> restoredAudio;
    for (const auto& entry : *audio)
    {
        const auto* value = entry.find("clip");
        if (value == nullptr)
            return fail(ErrorCode::invalidPayload, "missing key: clip");
        auto clip = AudioClip::fromValue(*value);
        if (!clip)
            return clip.error();
        auto rank = indexAt(entry, "index");
        if (!rank)
            return rank.error();
        restoredAudio.emplace_back(std::move(clip).value(), rank.value());
    }

    if (auto inserted = state.insertLane(std::move(lane).value(), index.value()); !inserted)
        return inserted;

    for (const auto& [placement, rank] : restoredPlacements)
    {
        if (auto placed = state.insertPlacement(placement, rank); !placed)
            return placed;
    }
    for (auto& [clip, rank] : restoredAudio)
    {
        if (auto placed = state.insertAudioClip(std::move(clip), rank); !placed)
            return placed;
    }
    return {};
}

// ---------------------------------------------------------------------------
// lane.rename
// ---------------------------------------------------------------------------

RenameLane::RenameLane(LaneId laneId, std::string name)
    : laneId_{laneId}
    , name_{std::move(name)}
{
}

Result<std::unique_ptr<Command>> RenameLane::fromPayload(const Value& payload)
{
    auto laneId = idAt<LaneId>(payload, "laneId");
    if (!laneId)
        return laneId.error();

    auto name = payload.stringAt("name");
    if (!name)
        return name.error();

    return std::unique_ptr<Command>{new RenameLane{laneId.value(), std::move(name).value()}};
}

Value RenameLane::payload() const
{
    return Value::object({{"laneId", Value{laneId_.toString()}}, {"name", Value{name_}}});
}

Result<Value> RenameLane::apply(ProjectState& state) const
{
    const auto* lane = state.findLane(laneId_);
    if (lane == nullptr)
        return fail(ErrorCode::notFound, "no such lane: " + laneId_.toString());

    auto record = Value::object({{"laneId", Value{laneId_.toString()}}, {"previousName", Value{lane->name}}});

    if (auto renamed = state.setLaneName(laneId_, name_); !renamed)
        return renamed.error();

    return record;
}

Result<void> RenameLane::revert(ProjectState& state, const Value& undoRecord) const
{
    auto laneId = idAt<LaneId>(undoRecord, "laneId");
    if (!laneId)
        return laneId.error();

    auto previous = undoRecord.stringAt("previousName");
    if (!previous)
        return previous.error();

    return state.setLaneName(laneId.value(), std::move(previous).value());
}

// ---------------------------------------------------------------------------
// lane.move
// ---------------------------------------------------------------------------

MoveLane::MoveLane(LaneId laneId, std::size_t index)
    : laneId_{laneId}
    , index_{index}
{
}

Result<std::unique_ptr<Command>> MoveLane::fromPayload(const Value& payload)
{
    auto laneId = idAt<LaneId>(payload, "laneId");
    if (!laneId)
        return laneId.error();

    auto index = indexAt(payload, "index");
    if (!index)
        return index.error();

    return std::unique_ptr<Command>{new MoveLane{laneId.value(), index.value()}};
}

Value MoveLane::payload() const
{
    return Value::object({{"laneId", Value{laneId_.toString()}}, {"index", indexValue(index_)}});
}

Result<Value> MoveLane::apply(ProjectState& state) const
{
    auto previous = state.laneIndex(laneId_);
    if (!previous)
        return previous.error();

    if (auto moved = state.moveLane(laneId_, index_); !moved)
        return moved.error();

    return Value::object(
        {{"laneId", Value{laneId_.toString()}}, {"previousIndex", indexValue(previous.value())}});
}

Result<void> MoveLane::revert(ProjectState& state, const Value& undoRecord) const
{
    auto laneId = idAt<LaneId>(undoRecord, "laneId");
    if (!laneId)
        return laneId.error();

    auto previous = indexAt(undoRecord, "previousIndex");
    if (!previous)
        return previous.error();

    return state.moveLane(laneId.value(), previous.value());
}

bool MoveLane::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const MoveLane*>(&newer);
    return other != nullptr && other->laneId_ == laneId_;
}

} // namespace daw::domain
