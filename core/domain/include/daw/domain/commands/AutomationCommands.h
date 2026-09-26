#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/Automation.h"

#include <memory>
#include <string_view>
#include <vector>

namespace daw::domain
{

// The verbs of automation (model in project/Automation.h). Every identifier
// comes from the caller. The two continuous ones, move_point and set_curve,
// coalesce per point inside a gesture, so dragging a point is one entry.

// automation.create_line — an empty line on a target. It drives nothing
// until it has a point; a target has one line at most.
class CreateAutomationLine final : public Command
{
public:
    static constexpr std::string_view commandType = "automation.create_line";

    CreateAutomationLine(AutomationLineId lineId, AutomationTarget target);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    AutomationLineId lineId_;
    AutomationTarget target_;
};

// automation.remove_line — the line and its points. The target goes back to
// its static value.
class RemoveAutomationLine final : public Command
{
public:
    static constexpr std::string_view commandType = "automation.remove_line";

    explicit RemoveAutomationLine(AutomationLineId lineId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    AutomationLineId lineId_;
};

// automation.add_point — a point on a line, never on the beat of another.
class AddAutomationPoint final : public Command
{
public:
    static constexpr std::string_view commandType = "automation.add_point";

    AddAutomationPoint(AutomationLineId lineId, AutomationPoint point);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    AutomationLineId lineId_;
    AutomationPoint point_;
};

// automation.move_point — beat and value together: a drag moves a point on
// both axes, and one command for both undoes into a point the user had.
class MoveAutomationPoint final : public Command
{
public:
    static constexpr std::string_view commandType = "automation.move_point";

    MoveAutomationPoint(AutomationLineId lineId, AutomationPointId pointId, double beats, double value);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;
    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

private:
    AutomationLineId lineId_;
    AutomationPointId pointId_;
    double beats_;
    double value_;
};

// automation.remove_point
class RemoveAutomationPoint final : public Command
{
public:
    static constexpr std::string_view commandType = "automation.remove_point";

    RemoveAutomationPoint(AutomationLineId lineId, AutomationPointId pointId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    AutomationLineId lineId_;
    AutomationPointId pointId_;
};

// automation.set_curve — the shape of the segment that leaves a point.
class SetAutomationCurve final : public Command
{
public:
    static constexpr std::string_view commandType = "automation.set_curve";

    SetAutomationCurve(AutomationLineId lineId, AutomationPointId pointId, double curve);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;
    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

private:
    AutomationLineId lineId_;
    AutomationPointId pointId_;
    double curve_;
};

// automation.write — what the copilot asks for: "a fade-out of the master
// over the last four bars" is one command, one entry, one Ctrl+Z.
//
// It replaces every point of the target's line between two beats, both
// included, by the points it carries, which must lie in that range. The line
// is created if the target has none; the identifier given is used only then,
// so the same payload replays onto the same project whether the line existed
// or not.
class WriteAutomation final : public Command
{
public:
    static constexpr std::string_view commandType = "automation.write";

    WriteAutomation(AutomationLineId lineId,
                    AutomationTarget target,
                    double fromBeats,
                    double toBeats,
                    std::vector<AutomationPoint> points);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    AutomationLineId lineId_;
    AutomationTarget target_;
    double fromBeats_;
    double toBeats_;
    std::vector<AutomationPoint> points_;
};

// For the commands that remove what a line aims at — a track, a bus, a
// plugin. The record holds each line with its rank; restoring puts them back
// in the same ranks, after the strip or the plugin is back. A record written
// before S13 has no such key and restores nothing.
[[nodiscard]] Value recordAutomation(const ProjectState& state, const std::vector<AutomationLineId>& lines);
[[nodiscard]] Result<void> restoreAutomation(ProjectState& state, const Value& undoRecord);

} // namespace daw::domain
