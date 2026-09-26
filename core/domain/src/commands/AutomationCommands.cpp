#include "daw/domain/commands/AutomationCommands.h"

#include "daw/domain/project/ProjectState.h"

#include <algorithm>
#include <cmath>
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

Result<AutomationTarget> targetAt(const Value& value)
{
    const auto* target = value.find("target");
    if (target == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: target");
    return AutomationTarget::fromValue(*target);
}

Value pointsValue(const std::vector<AutomationPoint>& points)
{
    Value::Array items;
    items.reserve(points.size());
    for (const auto& point : points)
        items.push_back(point.toValue());
    return Value::array(std::move(items));
}

Result<std::vector<AutomationPoint>> pointsAt(const Value& value, std::string_view key)
{
    const auto* found = value.find(key);
    if (found == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: " + std::string{key});
    const auto* items = found->asArray();
    if (items == nullptr)
        return fail(ErrorCode::invalidPayload, std::string{key} + " must be an array");

    std::vector<AutomationPoint> points;
    points.reserve(items->size());
    for (const auto& item : *items)
    {
        auto point = AutomationPoint::fromValue(item);
        if (!point)
            return point.error();
        points.push_back(point.value());
    }
    return points;
}

} // namespace

// ---------------------------------------------------------------------------
// automation.create_line
// ---------------------------------------------------------------------------

CreateAutomationLine::CreateAutomationLine(AutomationLineId lineId, AutomationTarget target)
    : lineId_{lineId}
    , target_{std::move(target)}
{
}

Result<std::unique_ptr<Command>> CreateAutomationLine::fromPayload(const Value& payload)
{
    auto lineId = idAt<AutomationLineId>(payload, "lineId");
    if (!lineId)
        return lineId.error();
    auto target = targetAt(payload);
    if (!target)
        return target.error();
    return std::unique_ptr<Command>{new CreateAutomationLine{lineId.value(), std::move(target).value()}};
}

Value CreateAutomationLine::payload() const
{
    return Value::object({{"lineId", Value{lineId_.toString()}}, {"target", target_.toValue()}});
}

Result<Value> CreateAutomationLine::apply(ProjectState& state) const
{
    AutomationLine line{};
    line.id = lineId_;
    line.target = target_;
    if (auto inserted = state.insertAutomationLine(std::move(line), state.automation().size()); !inserted)
        return inserted.error();
    return Value::object({{"lineId", Value{lineId_.toString()}}});
}

Result<void> CreateAutomationLine::revert(ProjectState& state, const Value& undoRecord) const
{
    auto lineId = idAt<AutomationLineId>(undoRecord, "lineId");
    if (!lineId)
        return lineId.error();
    return state.removeAutomationLine(lineId.value());
}

// ---------------------------------------------------------------------------
// automation.remove_line
// ---------------------------------------------------------------------------

RemoveAutomationLine::RemoveAutomationLine(AutomationLineId lineId)
    : lineId_{lineId}
{
}

Result<std::unique_ptr<Command>> RemoveAutomationLine::fromPayload(const Value& payload)
{
    auto lineId = idAt<AutomationLineId>(payload, "lineId");
    if (!lineId)
        return lineId.error();
    return std::unique_ptr<Command>{new RemoveAutomationLine{lineId.value()}};
}

Value RemoveAutomationLine::payload() const
{
    return Value::object({{"lineId", Value{lineId_.toString()}}});
}

Result<Value> RemoveAutomationLine::apply(ProjectState& state) const
{
    auto record = recordAutomation(state, {lineId_});
    if (record.size() == 0)
        return fail(ErrorCode::notFound, "no automation line " + lineId_.toString());
    if (auto removed = state.removeAutomationLine(lineId_); !removed)
        return removed.error();
    return Value::object({{"automation", std::move(record)}});
}

Result<void> RemoveAutomationLine::revert(ProjectState& state, const Value& undoRecord) const
{
    return restoreAutomation(state, undoRecord);
}

// ---------------------------------------------------------------------------
// automation.add_point
// ---------------------------------------------------------------------------

AddAutomationPoint::AddAutomationPoint(AutomationLineId lineId, AutomationPoint point)
    : lineId_{lineId}
    , point_{point}
{
}

Result<std::unique_ptr<Command>> AddAutomationPoint::fromPayload(const Value& payload)
{
    auto lineId = idAt<AutomationLineId>(payload, "lineId");
    if (!lineId)
        return lineId.error();
    auto pointId = idAt<AutomationPointId>(payload, "pointId");
    if (!pointId)
        return pointId.error();
    auto beats = payload.doubleAt("beats");
    if (!beats)
        return beats.error();
    auto value = payload.doubleAt("value");
    if (!value)
        return value.error();

    AutomationPoint point{};
    point.id = pointId.value();
    point.beats = beats.value();
    point.value = value.value();
    if (payload.contains("curve"))
    {
        auto curve = payload.doubleAt("curve");
        if (!curve)
            return curve.error();
        point.curve = curve.value();
    }
    return std::unique_ptr<Command>{new AddAutomationPoint{lineId.value(), point}};
}

Value AddAutomationPoint::payload() const
{
    return Value::object({{"lineId", Value{lineId_.toString()}},
                          {"pointId", Value{point_.id.toString()}},
                          {"beats", Value{point_.beats}},
                          {"value", Value{point_.value}},
                          {"curve", Value{point_.curve}}});
}

Result<Value> AddAutomationPoint::apply(ProjectState& state) const
{
    if (auto inserted = state.insertAutomationPoint(lineId_, point_); !inserted)
        return inserted.error();
    return Value::object({{"lineId", Value{lineId_.toString()}}, {"pointId", Value{point_.id.toString()}}});
}

Result<void> AddAutomationPoint::revert(ProjectState& state, const Value& undoRecord) const
{
    auto lineId = idAt<AutomationLineId>(undoRecord, "lineId");
    if (!lineId)
        return lineId.error();
    auto pointId = idAt<AutomationPointId>(undoRecord, "pointId");
    if (!pointId)
        return pointId.error();
    return state.removeAutomationPoint(lineId.value(), pointId.value());
}

// ---------------------------------------------------------------------------
// automation.move_point
// ---------------------------------------------------------------------------

MoveAutomationPoint::MoveAutomationPoint(AutomationLineId lineId,
                                         AutomationPointId pointId,
                                         double beats,
                                         double value)
    : lineId_{lineId}
    , pointId_{pointId}
    , beats_{beats}
    , value_{value}
{
}

Result<std::unique_ptr<Command>> MoveAutomationPoint::fromPayload(const Value& payload)
{
    auto lineId = idAt<AutomationLineId>(payload, "lineId");
    if (!lineId)
        return lineId.error();
    auto pointId = idAt<AutomationPointId>(payload, "pointId");
    if (!pointId)
        return pointId.error();
    auto beats = payload.doubleAt("beats");
    if (!beats)
        return beats.error();
    auto value = payload.doubleAt("value");
    if (!value)
        return value.error();
    return std::unique_ptr<Command>{
        new MoveAutomationPoint{lineId.value(), pointId.value(), beats.value(), value.value()}};
}

Value MoveAutomationPoint::payload() const
{
    return Value::object({{"lineId", Value{lineId_.toString()}},
                          {"pointId", Value{pointId_.toString()}},
                          {"beats", Value{beats_}},
                          {"value", Value{value_}}});
}

Result<Value> MoveAutomationPoint::apply(ProjectState& state) const
{
    const auto* line = state.findAutomationLine(lineId_);
    const auto* point = line != nullptr ? line->findPoint(pointId_) : nullptr;
    if (point == nullptr)
        return fail(ErrorCode::notFound, "no automation point " + pointId_.toString());

    auto record = Value::object({{"lineId", Value{lineId_.toString()}},
                                 {"pointId", Value{pointId_.toString()}},
                                 {"beats", Value{point->beats}},
                                 {"value", Value{point->value}}});

    if (auto moved = state.moveAutomationPoint(lineId_, pointId_, beats_, value_); !moved)
        return moved.error();
    return record;
}

Result<void> MoveAutomationPoint::revert(ProjectState& state, const Value& undoRecord) const
{
    auto lineId = idAt<AutomationLineId>(undoRecord, "lineId");
    if (!lineId)
        return lineId.error();
    auto pointId = idAt<AutomationPointId>(undoRecord, "pointId");
    if (!pointId)
        return pointId.error();
    auto beats = undoRecord.doubleAt("beats");
    if (!beats)
        return beats.error();
    auto value = undoRecord.doubleAt("value");
    if (!value)
        return value.error();
    return state.moveAutomationPoint(lineId.value(), pointId.value(), beats.value(), value.value());
}

bool MoveAutomationPoint::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const MoveAutomationPoint*>(&newer);
    return other != nullptr && other->lineId_ == lineId_ && other->pointId_ == pointId_;
}

// ---------------------------------------------------------------------------
// automation.remove_point
// ---------------------------------------------------------------------------

RemoveAutomationPoint::RemoveAutomationPoint(AutomationLineId lineId, AutomationPointId pointId)
    : lineId_{lineId}
    , pointId_{pointId}
{
}

Result<std::unique_ptr<Command>> RemoveAutomationPoint::fromPayload(const Value& payload)
{
    auto lineId = idAt<AutomationLineId>(payload, "lineId");
    if (!lineId)
        return lineId.error();
    auto pointId = idAt<AutomationPointId>(payload, "pointId");
    if (!pointId)
        return pointId.error();
    return std::unique_ptr<Command>{new RemoveAutomationPoint{lineId.value(), pointId.value()}};
}

Value RemoveAutomationPoint::payload() const
{
    return Value::object({{"lineId", Value{lineId_.toString()}}, {"pointId", Value{pointId_.toString()}}});
}

Result<Value> RemoveAutomationPoint::apply(ProjectState& state) const
{
    const auto* line = state.findAutomationLine(lineId_);
    const auto* point = line != nullptr ? line->findPoint(pointId_) : nullptr;
    if (point == nullptr)
        return fail(ErrorCode::notFound, "no automation point " + pointId_.toString());

    auto record = Value::object({{"lineId", Value{lineId_.toString()}}, {"point", point->toValue()}});
    if (auto removed = state.removeAutomationPoint(lineId_, pointId_); !removed)
        return removed.error();
    return record;
}

Result<void> RemoveAutomationPoint::revert(ProjectState& state, const Value& undoRecord) const
{
    auto lineId = idAt<AutomationLineId>(undoRecord, "lineId");
    if (!lineId)
        return lineId.error();
    const auto* pointValue = undoRecord.find("point");
    if (pointValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: point");
    auto point = AutomationPoint::fromValue(*pointValue);
    if (!point)
        return point.error();
    return state.insertAutomationPoint(lineId.value(), point.value());
}

// ---------------------------------------------------------------------------
// automation.set_curve
// ---------------------------------------------------------------------------

SetAutomationCurve::SetAutomationCurve(AutomationLineId lineId, AutomationPointId pointId, double curve)
    : lineId_{lineId}
    , pointId_{pointId}
    , curve_{curve}
{
}

Result<std::unique_ptr<Command>> SetAutomationCurve::fromPayload(const Value& payload)
{
    auto lineId = idAt<AutomationLineId>(payload, "lineId");
    if (!lineId)
        return lineId.error();
    auto pointId = idAt<AutomationPointId>(payload, "pointId");
    if (!pointId)
        return pointId.error();
    auto curve = payload.doubleAt("curve");
    if (!curve)
        return curve.error();
    return std::unique_ptr<Command>{new SetAutomationCurve{lineId.value(), pointId.value(), curve.value()}};
}

Value SetAutomationCurve::payload() const
{
    return Value::object({{"lineId", Value{lineId_.toString()}},
                          {"pointId", Value{pointId_.toString()}},
                          {"curve", Value{curve_}}});
}

Result<Value> SetAutomationCurve::apply(ProjectState& state) const
{
    const auto* line = state.findAutomationLine(lineId_);
    const auto* point = line != nullptr ? line->findPoint(pointId_) : nullptr;
    if (point == nullptr)
        return fail(ErrorCode::notFound, "no automation point " + pointId_.toString());

    auto record = Value::object({{"lineId", Value{lineId_.toString()}},
                                 {"pointId", Value{pointId_.toString()}},
                                 {"curve", Value{point->curve}}});
    if (auto set = state.setAutomationCurve(lineId_, pointId_, curve_); !set)
        return set.error();
    return record;
}

Result<void> SetAutomationCurve::revert(ProjectState& state, const Value& undoRecord) const
{
    auto lineId = idAt<AutomationLineId>(undoRecord, "lineId");
    if (!lineId)
        return lineId.error();
    auto pointId = idAt<AutomationPointId>(undoRecord, "pointId");
    if (!pointId)
        return pointId.error();
    auto curve = undoRecord.doubleAt("curve");
    if (!curve)
        return curve.error();
    return state.setAutomationCurve(lineId.value(), pointId.value(), curve.value());
}

bool SetAutomationCurve::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetAutomationCurve*>(&newer);
    return other != nullptr && other->lineId_ == lineId_ && other->pointId_ == pointId_;
}

// ---------------------------------------------------------------------------
// automation.write
// ---------------------------------------------------------------------------

WriteAutomation::WriteAutomation(AutomationLineId lineId,
                                 AutomationTarget target,
                                 double fromBeats,
                                 double toBeats,
                                 std::vector<AutomationPoint> points)
    : lineId_{lineId}
    , target_{std::move(target)}
    , fromBeats_{fromBeats}
    , toBeats_{toBeats}
    , points_{std::move(points)}
{
}

Result<std::unique_ptr<Command>> WriteAutomation::fromPayload(const Value& payload)
{
    auto lineId = idAt<AutomationLineId>(payload, "lineId");
    if (!lineId)
        return lineId.error();
    auto target = targetAt(payload);
    if (!target)
        return target.error();
    auto fromBeats = payload.doubleAt("fromBeats");
    if (!fromBeats)
        return fromBeats.error();
    auto toBeats = payload.doubleAt("toBeats");
    if (!toBeats)
        return toBeats.error();
    auto points = pointsAt(payload, "points");
    if (!points)
        return points.error();
    return std::unique_ptr<Command>{new WriteAutomation{lineId.value(),
                                                        std::move(target).value(),
                                                        fromBeats.value(),
                                                        toBeats.value(),
                                                        std::move(points).value()}};
}

Value WriteAutomation::payload() const
{
    return Value::object({{"lineId", Value{lineId_.toString()}},
                          {"target", target_.toValue()},
                          {"fromBeats", Value{fromBeats_}},
                          {"toBeats", Value{toBeats_}},
                          {"points", pointsValue(points_)}});
}

Result<Value> WriteAutomation::apply(ProjectState& state) const
{
    if (!std::isfinite(fromBeats_) || !std::isfinite(toBeats_) || fromBeats_ < 0.0 || toBeats_ < fromBeats_)
        return fail(ErrorCode::invalidArgument, "automation.write needs a range from 0 onwards, in order");
    for (const auto& point : points_)
    {
        if (point.beats < fromBeats_ || point.beats > toBeats_)
            return fail(ErrorCode::invalidArgument, "every written point lies inside the range it replaces");
    }
    if (auto target = state.checkAutomationTarget(target_); !target)
        return target.error();

    // The line the target already has, or a new one under the given name.
    const auto* existing = state.findAutomationLineFor(target_);
    const auto created = existing == nullptr;
    const auto lineId = created ? lineId_ : existing->id;

    // Built and checked whole before anything moves, so a refused write
    // leaves the line exactly as it was.
    AutomationLine written{};
    written.id = lineId;
    written.target = target_;
    std::vector<AutomationPoint> removed;
    if (!created)
    {
        for (const auto& point : existing->points)
        {
            if (point.beats >= fromBeats_ && point.beats <= toBeats_)
                removed.push_back(point);
            else
                written.points.push_back(point);
        }
    }
    written.points.insert(written.points.end(), points_.begin(), points_.end());
    std::sort(written.points.begin(),
              written.points.end(),
              [](const AutomationPoint& lhs, const AutomationPoint& rhs) { return lhs.beats < rhs.beats; });
    if (auto valid = written.validate(); !valid)
        return valid.error();

    if (created)
    {
        AutomationLine line{};
        line.id = lineId;
        line.target = target_;
        if (auto inserted = state.insertAutomationLine(std::move(line), state.automation().size()); !inserted)
            return inserted.error();
    }
    for (const auto& point : removed)
    {
        if (auto gone = state.removeAutomationPoint(lineId, point.id); !gone)
            return gone.error();
    }
    for (const auto& point : points_)
    {
        if (auto added = state.insertAutomationPoint(lineId, point); !added)
            return added.error();
    }

    Value::Array addedIds;
    for (const auto& point : points_)
        addedIds.push_back(Value{point.id.toString()});

    return Value::object({{"lineId", Value{lineId.toString()}},
                          {"created", Value{created}},
                          {"removed", pointsValue(removed)},
                          {"added", Value::array(std::move(addedIds))}});
}

Result<void> WriteAutomation::revert(ProjectState& state, const Value& undoRecord) const
{
    auto lineId = idAt<AutomationLineId>(undoRecord, "lineId");
    if (!lineId)
        return lineId.error();
    auto created = undoRecord.boolAt("created");
    if (!created)
        return created.error();

    if (created.value())
        return state.removeAutomationLine(lineId.value());

    if (const auto* added = undoRecord.find("added"); added != nullptr && added->asArray() != nullptr)
    {
        for (const auto& item : *added->asArray())
        {
            auto text = item.asString();
            if (!text)
                return text.error();
            auto pointId = AutomationPointId::parse(text.value());
            if (!pointId)
                return pointId.error();
            if (auto gone = state.removeAutomationPoint(lineId.value(), pointId.value()); !gone)
                return gone;
        }
    }

    auto removed = pointsAt(undoRecord, "removed");
    if (!removed)
        return removed.error();
    for (const auto& point : removed.value())
    {
        if (auto back = state.insertAutomationPoint(lineId.value(), point); !back)
            return back;
    }
    return {};
}

// ---------------------------------------------------------------------------
// cascade records
// ---------------------------------------------------------------------------

Value recordAutomation(const ProjectState& state, const std::vector<AutomationLineId>& lines)
{
    Value::Array items;
    for (std::size_t index = 0; index < state.automation().size(); ++index)
    {
        const auto& line = state.automation()[index];
        if (std::find(lines.begin(), lines.end(), line.id) == lines.end())
            continue;
        items.push_back(
            Value::object({{"index", Value{static_cast<std::int64_t>(index)}}, {"line", line.toValue()}}));
    }
    return Value::array(std::move(items));
}

Result<void> restoreAutomation(ProjectState& state, const Value& undoRecord)
{
    const auto* automation = undoRecord.find("automation");
    if (automation == nullptr)
        return {};
    const auto* items = automation->asArray();
    if (items == nullptr)
        return fail(ErrorCode::invalidPayload, "automation must be an array");

    // Ascending ranks, so each line lands where it was.
    for (const auto& item : *items)
    {
        auto index = item.intAt("index");
        if (!index)
            return index.error();
        if (index.value() < 0)
            return fail(ErrorCode::invalidPayload, "index is negative");
        const auto* lineValue = item.find("line");
        if (lineValue == nullptr)
            return fail(ErrorCode::invalidPayload, "missing key: line");
        auto line = AutomationLine::fromValue(*lineValue);
        if (!line)
            return line.error();
        if (auto inserted =
                state.insertAutomationLine(std::move(line).value(), static_cast<std::size_t>(index.value()));
            !inserted)
            return inserted;
    }
    return {};
}

} // namespace daw::domain
