#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <memory>
#include <string>

namespace daw::domain
{

// The verbs of the mixer that the channel had no use for: buses, routes,
// sends, solo. Volume, pan, mute, inserts and names are the channel's own
// commands, and they work on a bus and on the master unchanged — a bus is a
// strip like any other (see ProjectState::findStrip).
//
// As everywhere, no command engenders an identifier: the caller names the bus
// it creates.

// bus.add — appends an empty bus, going to the master.
//
// Removed by track.remove, like any strip: its undo record carries the routes
// and the sends that pointed at the bus.
class AddBus final : public Command
{
public:
    static constexpr std::string_view commandType = "bus.add";

    AddBus(TrackId busId, std::string name);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId busId_;
    std::string name_;
};

// track.set_output — sends a channel or a bus into a bus, or back to the
// master. A switch, not a movement: it never coalesces.
class SetTrackOutput final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_output";

    // A nil output is the master.
    SetTrackOutput(TrackId trackId, TrackId output);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
    TrackId output_;
};

// track.set_send — how much of a strip goes to a bus, after its fader. Makes
// the send the first time, changes its level after.
//
// A continuous control: it coalesces per strip and per bus, so a send knob
// turned for two seconds is one history entry — and undoing that entry takes
// the send away if the gesture made it, as a first touch of a plugin
// parameter gives the parameter back to its default.
class SetTrackSend final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_send";

    SetTrackSend(TrackId trackId, TrackId busId, double levelDb);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;
    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

private:
    TrackId trackId_;
    TrackId busId_;
    double levelDb_;
};

// track.remove_send — takes a send away. Its level and its rank go into the
// undo record, so an undo puts back the same send in the same place.
class RemoveTrackSend final : public Command
{
public:
    static constexpr std::string_view commandType = "track.remove_send";

    RemoveTrackSend(TrackId trackId, TrackId busId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
    TrackId busId_;
};

// track.set_solo — puts a strip in solo, or takes it out. A switch, like
// track.set_muted, and kept apart from it for the same reason: undoing a solo
// must not unmute anything. What a solo silences is ProjectState::isAudible's
// to say.
class SetTrackSolo final : public Command
{
public:
    static constexpr std::string_view commandType = "track.set_solo";

    SetTrackSolo(TrackId trackId, bool soloed);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    TrackId trackId_;
    bool soloed_;
};

} // namespace daw::domain
