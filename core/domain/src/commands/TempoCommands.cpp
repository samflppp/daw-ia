#include "daw/domain/commands/TempoCommands.h"

namespace daw::domain
{
namespace
{

Result<TempoPointId> pointIdAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto parsed = TempoPointId::parse(text.value());
    if (!parsed)
        return fail(parsed.error().code, std::string{key} + ": " + parsed.error().message);

    return parsed.value();
}

} // namespace

// ---------------------------------------------------------------------------
// tempo.insert
// ---------------------------------------------------------------------------

InsertTempoPoint::InsertTempoPoint(TempoPointId pointId, double startBeats, double beatsPerMinute)
    : pointId_{pointId}
    , startBeats_{startBeats}
    , beatsPerMinute_{beatsPerMinute}
{
}

Result<std::unique_ptr<Command>> InsertTempoPoint::fromPayload(const Value& payload)
{
    auto pointId = pointIdAt(payload, "pointId");
    if (!pointId)
        return pointId.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    auto beatsPerMinute = payload.doubleAt("beatsPerMinute");
    if (!beatsPerMinute)
        return beatsPerMinute.error();

    return std::unique_ptr<Command>{
        new InsertTempoPoint{pointId.value(), startBeats.value(), beatsPerMinute.value()}};
}

Value InsertTempoPoint::payload() const
{
    return Value::object({{"pointId", Value{pointId_.toString()}},
                          {"startBeats", Value{startBeats_}},
                          {"beatsPerMinute", Value{beatsPerMinute_}}});
}

Result<Value> InsertTempoPoint::apply(ProjectState& state) const
{
    TempoPoint point{};
    point.id = pointId_;
    point.startBeats = startBeats_;
    point.beatsPerMinute = beatsPerMinute_;

    if (auto inserted = state.insertTempoPoint(point); !inserted)
        return inserted.error();

    return Value::object({{"pointId", Value{pointId_.toString()}}});
}

Result<void> InsertTempoPoint::revert(ProjectState& state, const Value& undoRecord) const
{
    auto pointId = pointIdAt(undoRecord, "pointId");
    if (!pointId)
        return pointId.error();

    return state.removeTempoPoint(pointId.value());
}

// ---------------------------------------------------------------------------
// tempo.remove
// ---------------------------------------------------------------------------

RemoveTempoPoint::RemoveTempoPoint(TempoPointId pointId)
    : pointId_{pointId}
{
}

Result<std::unique_ptr<Command>> RemoveTempoPoint::fromPayload(const Value& payload)
{
    auto pointId = pointIdAt(payload, "pointId");
    if (!pointId)
        return pointId.error();

    return std::unique_ptr<Command>{new RemoveTempoPoint{pointId.value()}};
}

Value RemoveTempoPoint::payload() const
{
    return Value::object({{"pointId", Value{pointId_.toString()}}});
}

Result<Value> RemoveTempoPoint::apply(ProjectState& state) const
{
    // Read before removing: the point is what the undo has to put back, and it
    // no longer exists once the removal has run.
    auto point = state.tempoPoint(pointId_);
    if (!point)
        return point.error();

    if (auto removed = state.removeTempoPoint(pointId_); !removed)
        return removed.error();

    return Value::object({{"point", point.value().toValue()}});
}

Result<void> RemoveTempoPoint::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* stored = undoRecord.find("point");
    if (stored == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: point");

    auto point = TempoPoint::fromValue(*stored);
    if (!point)
        return point.error();

    return state.insertTempoPoint(point.value());
}

// ---------------------------------------------------------------------------
// tempo.set_bpm
// ---------------------------------------------------------------------------

SetTempoPointBpm::SetTempoPointBpm(TempoPointId pointId, double beatsPerMinute)
    : pointId_{pointId}
    , beatsPerMinute_{beatsPerMinute}
{
}

Result<std::unique_ptr<Command>> SetTempoPointBpm::fromPayload(const Value& payload)
{
    auto pointId = pointIdAt(payload, "pointId");
    if (!pointId)
        return pointId.error();

    auto beatsPerMinute = payload.doubleAt("beatsPerMinute");
    if (!beatsPerMinute)
        return beatsPerMinute.error();

    return std::unique_ptr<Command>{new SetTempoPointBpm{pointId.value(), beatsPerMinute.value()}};
}

Value SetTempoPointBpm::payload() const
{
    return Value::object(
        {{"pointId", Value{pointId_.toString()}}, {"beatsPerMinute", Value{beatsPerMinute_}}});
}

Result<Value> SetTempoPointBpm::apply(ProjectState& state) const
{
    auto previous = state.tempoPoint(pointId_);
    if (!previous)
        return previous.error();

    if (auto applied = state.setTempoPointBpm(pointId_, beatsPerMinute_); !applied)
        return applied.error();

    return Value::object({{"pointId", Value{pointId_.toString()}},
                          {"previousBeatsPerMinute", Value{previous.value().beatsPerMinute}}});
}

Result<void> SetTempoPointBpm::revert(ProjectState& state, const Value& undoRecord) const
{
    auto pointId = pointIdAt(undoRecord, "pointId");
    if (!pointId)
        return pointId.error();

    auto previous = undoRecord.doubleAt("previousBeatsPerMinute");
    if (!previous)
        return previous.error();

    return state.setTempoPointBpm(pointId.value(), previous.value());
}

bool SetTempoPointBpm::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetTempoPointBpm*>(&newer);
    return other != nullptr && other->pointId_ == pointId_;
}

// ---------------------------------------------------------------------------
// tempo.move
// ---------------------------------------------------------------------------

MoveTempoPoint::MoveTempoPoint(TempoPointId pointId, double startBeats)
    : pointId_{pointId}
    , startBeats_{startBeats}
{
}

Result<std::unique_ptr<Command>> MoveTempoPoint::fromPayload(const Value& payload)
{
    auto pointId = pointIdAt(payload, "pointId");
    if (!pointId)
        return pointId.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    return std::unique_ptr<Command>{new MoveTempoPoint{pointId.value(), startBeats.value()}};
}

Value MoveTempoPoint::payload() const
{
    return Value::object({{"pointId", Value{pointId_.toString()}}, {"startBeats", Value{startBeats_}}});
}

Result<Value> MoveTempoPoint::apply(ProjectState& state) const
{
    auto previous = state.tempoPoint(pointId_);
    if (!previous)
        return previous.error();

    if (auto moved = state.moveTempoPoint(pointId_, startBeats_); !moved)
        return moved.error();

    return Value::object({{"pointId", Value{pointId_.toString()}},
                          {"previousStartBeats", Value{previous.value().startBeats}}});
}

Result<void> MoveTempoPoint::revert(ProjectState& state, const Value& undoRecord) const
{
    auto pointId = pointIdAt(undoRecord, "pointId");
    if (!pointId)
        return pointId.error();

    auto previous = undoRecord.doubleAt("previousStartBeats");
    if (!previous)
        return previous.error();

    return state.moveTempoPoint(pointId.value(), previous.value());
}

bool MoveTempoPoint::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const MoveTempoPoint*>(&newer);
    return other != nullptr && other->pointId_ == pointId_;
}

// ---------------------------------------------------------------------------
// project.set_time_signature
// ---------------------------------------------------------------------------

SetTimeSignature::SetTimeSignature(TimeSignature signature)
    : signature_{signature}
{
}

Result<std::unique_ptr<Command>> SetTimeSignature::fromPayload(const Value& payload)
{
    auto signature = TimeSignature::fromValue(payload);
    if (!signature)
        return signature.error();

    return std::unique_ptr<Command>{new SetTimeSignature{signature.value()}};
}

Value SetTimeSignature::payload() const
{
    return signature_.toValue();
}

Result<Value> SetTimeSignature::apply(ProjectState& state) const
{
    const auto previous = state.timeSignature();
    if (auto applied = state.setTimeSignature(signature_); !applied)
        return applied.error();

    return previous.toValue();
}

Result<void> SetTimeSignature::revert(ProjectState& state, const Value& undoRecord) const
{
    auto previous = TimeSignature::fromValue(undoRecord);
    if (!previous)
        return previous.error();

    return state.setTimeSignature(previous.value());
}

bool SetTimeSignature::canCoalesceWith(const Command& newer) const noexcept
{
    return dynamic_cast<const SetTimeSignature*>(&newer) != nullptr;
}

} // namespace daw::domain
