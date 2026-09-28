#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>
#include <string>

namespace daw::domain
{

// The verbs of the playlist lines.
//
// A line is where blocks are filed, and nothing it does changes a sound. Every
// identifier comes from the caller, like everywhere else in this project: a
// line invented at apply() time would differ between a run and its replay.

// lane.create — an empty line, at a rank. Beyond the last line it appends.
class CreateLane final : public Command
{
public:
    static constexpr std::string_view commandType = "lane.create";

    CreateLane(LaneId laneId, std::string name, std::size_t index);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    LaneId laneId_;
    std::string name_;
    std::size_t index_;
};

// lane.remove — the line and every block filed on it, as FL does. The undo
// record carries the line, its rank, and each block with its rank, so an undo
// gives back the same playlist and not merely an equivalent one.
class RemoveLane final : public Command
{
public:
    static constexpr std::string_view commandType = "lane.remove";

    explicit RemoveLane(LaneId laneId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    LaneId laneId_;
};

// lane.rename — what the line is called. Empty means "shown by its rank".
class RenameLane final : public Command
{
public:
    static constexpr std::string_view commandType = "lane.rename";

    RenameLane(LaneId laneId, std::string name);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    LaneId laneId_;
    std::string name_;
};

// lane.move — another rank for a line. Its blocks go with it, since they name
// it and not a rank. Coalesces per line: dragging a header past five lines is
// one history entry.
class MoveLane final : public Command
{
public:
    static constexpr std::string_view commandType = "lane.move";

    MoveLane(LaneId laneId, std::size_t index);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;
    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

private:
    LaneId laneId_;
    std::size_t index_;
};

} // namespace daw::domain
