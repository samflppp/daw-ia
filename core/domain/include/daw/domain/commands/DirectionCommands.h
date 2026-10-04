#pragma once

#include "daw/domain/command/Command.h"
#include "daw/domain/direction/Direction.h"

#include <memory>
#include <string_view>

namespace daw::domain
{

// direction.set (S22) — the project's direction, whole: its references (read
// into numbers, never their audio), the person's corrections, the amount.
// One verb for every change of it — a reference added, a weight moved, a
// tempo corrected —, each one Ctrl+Z, because the panel always writes the
// direction as it now stands. An empty direction takes the project back to
// having none.
//
// The undo record carries the direction before: a reference's reading cannot
// be computed again without its file, which the project does not keep.
class SetDirection final : public Command
{
public:
    static constexpr std::string_view commandType = "direction.set";

    explicit SetDirection(direction::Direction direction);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    direction::Direction direction_;
};

} // namespace daw::domain
