#include "daw/domain/commands/PluginCommands.h"

#include "daw/domain/commands/AutomationCommands.h"

#include <cstdint>
#include <utility>

namespace daw::domain
{
namespace
{

Result<PluginId> pluginIdAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto parsed = PluginId::parse(text.value());
    if (!parsed)
        return fail(parsed.error().code, std::string{key} + ": " + parsed.error().message);

    return parsed.value();
}

} // namespace

// ---------------------------------------------------------------------------
// InsertPlugin
// ---------------------------------------------------------------------------

InsertPlugin::InsertPlugin(TrackId trackId, PluginInstance plugin, std::size_t index)
    : trackId_{trackId}
    , plugin_{std::move(plugin)}
    , index_{index}
{
}

Result<std::unique_ptr<Command>> InsertPlugin::fromPayload(const Value& payload)
{
    auto trackText = payload.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    const auto* pluginValue = payload.find("plugin");
    if (pluginValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: plugin");

    auto plugin = PluginInstance::fromValue(*pluginValue);
    if (!plugin)
        return plugin.error();

    auto index = payload.intAt("index");
    if (!index)
        return index.error();

    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    return std::unique_ptr<Command>{new InsertPlugin{
        trackId.value(), std::move(plugin).value(), static_cast<std::size_t>(index.value())}};
}

Value InsertPlugin::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}},
                          {"plugin", plugin_.toValue()},
                          {"index", Value{static_cast<std::int64_t>(index_)}}});
}

Result<Value> InsertPlugin::apply(ProjectState& state) const
{
    auto inserted = state.insertPlugin(trackId_, plugin_, index_);
    if (!inserted)
        return inserted.error();

    return Value::object({{"pluginId", Value{plugin_.id.toString()}}});
}

Result<void> InsertPlugin::revert(ProjectState& state, const Value& undoRecord) const
{
    auto pluginId = pluginIdAt(undoRecord, "pluginId");
    if (!pluginId)
        return pluginId.error();

    return state.removePlugin(pluginId.value());
}

// ---------------------------------------------------------------------------
// RemovePlugin
// ---------------------------------------------------------------------------

RemovePlugin::RemovePlugin(PluginId pluginId)
    : pluginId_{pluginId}
{
}

Result<std::unique_ptr<Command>> RemovePlugin::fromPayload(const Value& payload)
{
    auto pluginId = pluginIdAt(payload, "pluginId");
    if (!pluginId)
        return pluginId.error();

    return std::unique_ptr<Command>{new RemovePlugin{pluginId.value()}};
}

Value RemovePlugin::payload() const
{
    return Value::object({{"pluginId", Value{pluginId_.toString()}}});
}

Result<Value> RemovePlugin::apply(ProjectState& state) const
{
    auto location = state.pluginLocation(pluginId_);
    if (!location)
        return location.error();

    const auto* plugin = state.findPlugin(pluginId_);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + pluginId_.toString());

    auto undoRecord = Value::object({{"trackId", Value{location.value().trackId.toString()}},
                                     {"index", Value{static_cast<std::int64_t>(location.value().index)}},
                                     {"plugin", plugin->toValue()}});

    // The lines of its parameters go with it, and come back with it.
    if (auto lines = state.automationOfPlugin(pluginId_); !lines.empty())
        static_cast<void>(undoRecord.set("automation", recordAutomation(state, lines)));

    auto removed = state.removePlugin(pluginId_);
    if (!removed)
        return removed.error();

    return undoRecord;
}

Result<void> RemovePlugin::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackText = undoRecord.stringAt("trackId");
    if (!trackText)
        return trackText.error();

    auto trackId = TrackId::parse(trackText.value());
    if (!trackId)
        return fail(trackId.error().code, "trackId: " + trackId.error().message);

    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();

    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    const auto* pluginValue = undoRecord.find("plugin");
    if (pluginValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: plugin");

    auto plugin = PluginInstance::fromValue(*pluginValue);
    if (!plugin)
        return plugin.error();

    if (auto inserted = state.insertPlugin(
            trackId.value(), std::move(plugin).value(), static_cast<std::size_t>(index.value()));
        !inserted)
        return inserted;

    return restoreAutomation(state, undoRecord);
}

// ---------------------------------------------------------------------------
// SetPluginBypassed
// ---------------------------------------------------------------------------

SetPluginBypassed::SetPluginBypassed(PluginId pluginId, bool bypassed)
    : pluginId_{pluginId}
    , bypassed_{bypassed}
{
}

Result<std::unique_ptr<Command>> SetPluginBypassed::fromPayload(const Value& payload)
{
    auto pluginId = pluginIdAt(payload, "pluginId");
    if (!pluginId)
        return pluginId.error();

    auto bypassed = payload.boolAt("bypassed");
    if (!bypassed)
        return bypassed.error();

    return std::unique_ptr<Command>{new SetPluginBypassed{pluginId.value(), bypassed.value()}};
}

Value SetPluginBypassed::payload() const
{
    return Value::object({{"pluginId", Value{pluginId_.toString()}}, {"bypassed", Value{bypassed_}}});
}

Result<Value> SetPluginBypassed::apply(ProjectState& state) const
{
    const auto* plugin = state.findPlugin(pluginId_);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + pluginId_.toString());

    const bool previous = plugin->bypassed;

    auto applied = state.setPluginBypassed(pluginId_, bypassed_);
    if (!applied)
        return applied.error();

    return Value::object({{"bypassed", Value{previous}}});
}

Result<void> SetPluginBypassed::revert(ProjectState& state, const Value& undoRecord) const
{
    auto previous = undoRecord.boolAt("bypassed");
    if (!previous)
        return previous.error();

    return state.setPluginBypassed(pluginId_, previous.value());
}

// ---------------------------------------------------------------------------
// MovePlugin
// ---------------------------------------------------------------------------

MovePlugin::MovePlugin(PluginId pluginId, std::size_t index)
    : pluginId_{pluginId}
    , index_{index}
{
}

Result<std::unique_ptr<Command>> MovePlugin::fromPayload(const Value& payload)
{
    auto pluginId = pluginIdAt(payload, "pluginId");
    if (!pluginId)
        return pluginId.error();

    auto index = payload.intAt("index");
    if (!index)
        return index.error();

    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    return std::unique_ptr<Command>{
        new MovePlugin{pluginId.value(), static_cast<std::size_t>(index.value())}};
}

Value MovePlugin::payload() const
{
    return Value::object(
        {{"pluginId", Value{pluginId_.toString()}}, {"index", Value{static_cast<std::int64_t>(index_)}}});
}

Result<Value> MovePlugin::apply(ProjectState& state) const
{
    auto from = state.movePlugin(pluginId_, index_);
    if (!from)
        return from.error();

    return Value::object({{"index", Value{static_cast<std::int64_t>(from.value())}}});
}

Result<void> MovePlugin::revert(ProjectState& state, const Value& undoRecord) const
{
    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();

    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    auto moved = state.movePlugin(pluginId_, static_cast<std::size_t>(index.value()));
    if (!moved)
        return moved.error();
    return {};
}

// ---------------------------------------------------------------------------
// SetPluginParameter
// ---------------------------------------------------------------------------

SetPluginParameter::SetPluginParameter(PluginId pluginId, std::string paramId, double value)
    : pluginId_{pluginId}
    , paramId_{std::move(paramId)}
    , value_{value}
{
}

Result<std::unique_ptr<Command>> SetPluginParameter::fromPayload(const Value& payload)
{
    auto pluginId = pluginIdAt(payload, "pluginId");
    if (!pluginId)
        return pluginId.error();

    auto paramId = payload.stringAt("paramId");
    if (!paramId)
        return paramId.error();

    auto value = payload.doubleAt("value");
    if (!value)
        return value.error();

    return std::unique_ptr<Command>{
        new SetPluginParameter{pluginId.value(), std::move(paramId).value(), value.value()}};
}

Value SetPluginParameter::payload() const
{
    return Value::object(
        {{"pluginId", Value{pluginId_.toString()}}, {"paramId", Value{paramId_}}, {"value", Value{value_}}});
}

Result<Value> SetPluginParameter::apply(ProjectState& state) const
{
    const auto* plugin = state.findPlugin(pluginId_);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + pluginId_.toString());

    const auto* previous = plugin->findParam(paramId_);
    const bool existed = previous != nullptr;
    const double previousValue = existed ? previous->value : 0.0;

    auto applied = state.setPluginParameter(pluginId_, paramId_, value_);
    if (!applied)
        return applied.error();

    return Value::object({{"existed", Value{existed}}, {"value", Value{previousValue}}});
}

Result<void> SetPluginParameter::revert(ProjectState& state, const Value& undoRecord) const
{
    auto existed = undoRecord.boolAt("existed");
    if (!existed)
        return existed.error();

    if (!existed.value())
        return state.clearPluginParameter(pluginId_, paramId_);

    auto previous = undoRecord.doubleAt("value");
    if (!previous)
        return previous.error();

    return state.setPluginParameter(pluginId_, paramId_, previous.value());
}

bool SetPluginParameter::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetPluginParameter*>(&newer);
    return other != nullptr && other->pluginId_ == pluginId_ && other->paramId_ == paramId_;
}

// ---------------------------------------------------------------------------
// CapturePluginState
// ---------------------------------------------------------------------------

CapturePluginState::CapturePluginState(PluginId pluginId, StateBlobRef state)
    : pluginId_{pluginId}
    , state_{std::move(state)}
{
}

Result<std::unique_ptr<Command>> CapturePluginState::fromPayload(const Value& payload)
{
    auto pluginId = pluginIdAt(payload, "pluginId");
    if (!pluginId)
        return pluginId.error();

    const auto* stateValue = payload.find("state");
    if (stateValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: state");

    auto state = StateBlobRef::fromValue(*stateValue);
    if (!state)
        return state.error();

    return std::unique_ptr<Command>{new CapturePluginState{pluginId.value(), std::move(state).value()}};
}

Value CapturePluginState::payload() const
{
    return Value::object({{"pluginId", Value{pluginId_.toString()}}, {"state", state_.toValue()}});
}

Result<Value> CapturePluginState::apply(ProjectState& state) const
{
    const auto* plugin = state.findPlugin(pluginId_);
    if (plugin == nullptr)
        return fail(ErrorCode::notFound, "no such plugin: " + pluginId_.toString());

    auto undoRecord = Value::object({{"state", plugin->state.toValue()}});

    auto applied = state.setPluginState(pluginId_, state_);
    if (!applied)
        return applied.error();

    return undoRecord;
}

Result<void> CapturePluginState::revert(ProjectState& state, const Value& undoRecord) const
{
    const auto* stateValue = undoRecord.find("state");
    if (stateValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: state");

    auto previous = StateBlobRef::fromValue(*stateValue);
    if (!previous)
        return previous.error();

    // The store is content-addressed and never rewrites a blob, so the bytes
    // the previous digest refers to are still there. Undoing a capture is
    // therefore always possible, whatever happened in between.
    return state.setPluginState(pluginId_, std::move(previous).value());
}

} // namespace daw::domain
