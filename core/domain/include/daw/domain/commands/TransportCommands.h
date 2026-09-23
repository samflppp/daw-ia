#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

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
// transport.set_loop — plays a range over and over, or stops looping.
//
// Transient like the rest of the transport: a loop is not something a project
// remembers, and replaying one from a journal would start a beat looping the
// moment the project was reopened.
class TransportSetLoop final : public Command
{
public:
    static constexpr std::string_view commandType = "transport.set_loop";

    TransportSetLoop(bool looping, double startBeats, double endBeats);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] HistoryPolicy historyPolicy() const noexcept override { return HistoryPolicy::transient; }
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    bool looping_;
    double startBeats_;
    double endBeats_;
};

// transport.set_mode — pattern or song.
//
// Transient like the rest of the transport. The pattern pattern mode plays is
// in the payload, never read from a screen: the command means the same thing
// whoever sends it, the rack, the transport button or the copilot.
//
// Switching mode brings the playhead back to the start. The two modes share no
// timeline — beat 40 of the song is nowhere in a four-beat pattern — so there
// is no position that means the same thing on both sides.
class TransportSetMode final : public Command
{
public:
    static constexpr std::string_view commandType = "transport.set_mode";
    static constexpr std::string_view songMode = "song";
    static constexpr std::string_view patternMode = "pattern";

    TransportSetMode(PlayMode mode, PatternId auditioned);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] HistoryPolicy historyPolicy() const noexcept override { return HistoryPolicy::transient; }
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PlayMode mode_;
    PatternId auditioned_;
};

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
