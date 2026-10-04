#include "daw/domain/commands/DirectionCommands.h"

#include "daw/domain/project/ProjectState.h"

#include <utility>

namespace daw::domain
{

SetDirection::SetDirection(direction::Direction direction)
    : direction_{std::move(direction)}
{
}

Result<std::unique_ptr<Command>> SetDirection::fromPayload(const Value& payload)
{
    const auto* value = payload.find("direction");
    if (value == nullptr)
        return fail(ErrorCode::invalidPayload, "direction.set needs a direction");
    auto direction = direction::Direction::fromValue(*value);
    if (!direction)
        return direction.error();
    return std::unique_ptr<Command>{new SetDirection{std::move(direction).value()}};
}

Value SetDirection::payload() const
{
    return Value::object({{"direction", direction_.toValue()}});
}

Result<Value> SetDirection::apply(ProjectState& state) const
{
    const auto before = state.direction().toValue();
    state.setDirection(direction_);
    return Value::object({{"before", before}});
}

Result<void> SetDirection::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* before = undoRecord.find("before");
    if (before == nullptr)
        return fail(ErrorCode::invalidPayload, "direction.set's record holds the direction before");
    auto direction = direction::Direction::fromValue(*before);
    if (!direction)
        return direction.error();
    state.setDirection(std::move(direction).value());
    return {};
}

} // namespace daw::domain
