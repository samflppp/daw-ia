#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <memory>
#include <string>

namespace daw::domain
{

// The five commands that host a plugin. None of them engenders an identifier or
// a digest: the caller computes both before calling execute(), exactly as it
// does for a clip. The engine is what knows how to scan and how to hash; the
// domain only records the decision.

// plugin.insert — puts a plugin instance in a track's chain, at an index.
class InsertPlugin final : public Command
{
public:
    static constexpr std::string_view commandType = "plugin.insert";

    InsertPlugin(TrackId trackId, PluginInstance plugin, std::size_t index);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    [[nodiscard]] TrackId trackId() const noexcept { return trackId_; }
    [[nodiscard]] const PluginInstance& plugin() const noexcept { return plugin_; }

private:
    TrackId trackId_;
    PluginInstance plugin_;
    std::size_t index_;
};

// plugin.remove — takes a plugin out of its chain.
//
// The undo record carries the whole instance and its index, not just the
// identifier: parameters and captured state have to come back where they were,
// and the command itself does not know them.
class RemovePlugin final : public Command
{
public:
    static constexpr std::string_view commandType = "plugin.remove";

    explicit RemovePlugin(PluginId pluginId);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PluginId pluginId_;
};

// plugin.move — moves a plugin within its own chain (S24, decided on
// 6 October 2026). Removing and inserting again would give back the same
// project, but the engine would load the plugin anew: a heavy instrument or
// effect silent for seconds, its tails cut, whatever was turned in its own
// window since the last capture lost, and two entries in the journal where
// the person did one thing. This one keeps the instance: the engine moves the
// plugin it already holds. Between two tracks it is still a removal and an
// insertion, in one group.
//
// `index` is where it ends, in the chain without it counted again: 0 is first.
// The undo record is the index it was at.
class MovePlugin final : public Command
{
public:
    static constexpr std::string_view commandType = "plugin.move";

    MovePlugin(PluginId pluginId, std::size_t index);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PluginId pluginId_;
    std::size_t index_;
};

// plugin.set_bypassed — kept apart from set_parameter on purpose: bypass is a
// property of the chain, not a parameter of the plugin. Not coalescable: a
// bypass is a switch, not a movement.
class SetPluginBypassed final : public Command
{
public:
    static constexpr std::string_view commandType = "plugin.set_bypassed";

    SetPluginBypassed(PluginId pluginId, bool bypassed);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PluginId pluginId_;
    bool bypassed_;
};

// plugin.set_parameter — the light, continuous half of plugin state.
//
// One command per value, merged per gesture: a knob sweep leaves one history
// entry, like a fader drag does. The value is normalised, because that is the
// only scale every plugin format agrees on.
//
// The undo record says whether the parameter existed before. Undoing the very
// first touch removes the entry instead of writing a default value into it,
// which is what gives the parameter back to the captured blob.
class SetPluginParameter final : public Command
{
public:
    static constexpr std::string_view commandType = "plugin.set_parameter";

    SetPluginParameter(PluginId pluginId, std::string paramId, double value);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

    // Absorbs another movement of the same parameter of the same plugin, and
    // nothing else. Two different parameters moved inside one gesture stay two
    // history entries, because undoing one must not move the other.
    [[nodiscard]] bool canCoalesceWith(const Command& newer) const noexcept override;

    [[nodiscard]] PluginId pluginId() const noexcept { return pluginId_; }
    [[nodiscard]] const std::string& paramId() const noexcept { return paramId_; }
    [[nodiscard]] double value() const noexcept { return value_; }

private:
    PluginId pluginId_;
    std::string paramId_;
    double value_;
};

// plugin.capture_state — the heavy, punctual half.
//
// The bytes are already in the content-addressed store when this runs: the
// payload carries the digest and the size, never the blob. Reserved for saving
// and for closing a session — a capture per knob movement would put megabytes
// in the journal for nothing.
class CapturePluginState final : public Command
{
public:
    static constexpr std::string_view commandType = "plugin.capture_state";

    CapturePluginState(PluginId pluginId, StateBlobRef state);

    [[nodiscard]] static Result<std::unique_ptr<Command>> fromPayload(const Value& payload);

    [[nodiscard]] std::string_view type() const noexcept override { return commandType; }
    [[nodiscard]] Reach reach() const noexcept override { return Reach::mix; }
    [[nodiscard]] Value payload() const override;
    [[nodiscard]] Result<Value> apply(ProjectState& state) const override;
    [[nodiscard]] Result<void> revert(ProjectState& state, const Value& undoRecord) const override;

private:
    PluginId pluginId_;
    StateBlobRef state_;
};

} // namespace daw::domain
