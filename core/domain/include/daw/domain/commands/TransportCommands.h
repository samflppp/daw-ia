#pragma once

#include "daw/domain/command/Command.h"

#include <memory>

namespace daw::domain
{

// The transport commands are the one place where the rule of S2 bends, and the
// bend is explicit: they are HistoryPolicy::transient.
//
// They still go through the bus — validated, notified, visible from the UI and
// from MCP — but they create no history entry and no journal entry. Undoing a
// "play" has no meaning, and a journal that contained one would start making
// noise the moment a project was replayed.
//
// revert() is never called on them. It returns an error rather than doing
// nothing, so a mistake upstream is loud instead of silent.

class TransportPlay final : public Command
{
public:
    static constexpr std::string_view commandType = "transport.play";

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] HistoryPolicy historyPolicy() const noexcept override { return HistoryPolicy::transient; }
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;
};

class TransportStop final : public Command
{
public:
    static constexpr std::string_view commandType = "transport.stop";

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] HistoryPolicy historyPolicy() const noexcept override { return HistoryPolicy::transient; }
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;
};

// Moves the playhead. Separate from stop on purpose: stop returns to the
// start, this goes anywhere -- including to the start while playback runs on.
class TransportSetPosition final : public Command
{
public:
    static constexpr std::string_view commandType = "transport.set_position";

    explicit TransportSetPosition(double positionBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] HistoryPolicy historyPolicy() const noexcept override { return HistoryPolicy::transient; }
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] double positionBeats() const noexcept { return positionBeats_; }

private:
    double positionBeats_;
};

} // namespace daw::domain
