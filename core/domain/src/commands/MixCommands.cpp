#include "daw/domain/commands/MixCommands.h"

#include <cstdint>
#include <utility>

namespace daw::domain
{
namespace
{

Result<TrackId> trackIdAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto parsed = TrackId::parse(text.value());
    if (!parsed)
        return fail(parsed.error().code, std::string{key} + ": " + parsed.error().message);

    return parsed.value();
}

// An output is a bus, or the master, written as an empty string: a copilot
// asked to "send the drums back to the master" should not have to know the
// master's identifier.
Result<TrackId> outputAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    if (text.value().empty())
        return TrackId{};

    auto parsed = trackIdAt(value, key);
    if (!parsed)
        return parsed.error();

    // The master named by its identifier is the master too.
    return parsed.value() == ProjectState::masterTrackId() ? TrackId{} : parsed.value();
}

std::string outputText(TrackId output)
{
    return output.isNil() ? std::string{} : output.toString();
}

} // namespace

// ---------------------------------------------------------------------------
// bus.add
// ---------------------------------------------------------------------------

AddBus::AddBus(TrackId busId, std::string name)
    : busId_{busId}
    , name_{std::move(name)}
{
}

Result<std::unique_ptr<Command>> AddBus::fromPayload(const Value& payload)
{
    auto busId = trackIdAt(payload, "busId");
    if (!busId)
        return busId.error();

    auto name = payload.stringAt("name");
    if (!name)
        return name.error();

    return std::unique_ptr<Command>{new AddBus{busId.value(), name.value()}};
}

Value AddBus::payload() const
{
    return Value::object({{"busId", Value{busId_.toString()}}, {"name", Value{name_}}});
}

Result<Value> AddBus::apply(ProjectState& state) const
{
    Track bus{};
    bus.id = busId_;
    bus.name = name_;

    if (auto added = state.insertBus(bus, state.buses().size()); !added)
        return added.error();

    return Value::object({{"busId", Value{busId_.toString()}}});
}

Result<void> AddBus::revert(ProjectState& state, const Value& undoRecord) const
{
    auto busId = trackIdAt(undoRecord, "busId");
    if (!busId)
        return busId.error();

    return state.removeBus(busId.value());
}

// ---------------------------------------------------------------------------
// track.set_output
// ---------------------------------------------------------------------------

SetTrackOutput::SetTrackOutput(TrackId trackId, TrackId output)
    : trackId_{trackId}
    , output_{output}
{
}

Result<std::unique_ptr<Command>> SetTrackOutput::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto output = outputAt(payload, "output");
    if (!output)
        return output.error();

    return std::unique_ptr<Command>{new SetTrackOutput{trackId.value(), output.value()}};
}

Value SetTrackOutput::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"output", Value{outputText(output_)}}});
}

Result<Value> SetTrackOutput::apply(ProjectState& state) const
{
    const auto* strip = state.findStrip(trackId_);
    if (strip == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId_.toString());

    const auto previous = strip->output;
    if (auto applied = state.setTrackOutput(trackId_, output_); !applied)
        return applied.error();

    return Value::object(
        {{"trackId", Value{trackId_.toString()}}, {"previousOutput", Value{outputText(previous)}}});
}

Result<void> SetTrackOutput::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto previous = outputAt(undoRecord, "previousOutput");
    if (!previous)
        return previous.error();

    return state.setTrackOutput(trackId.value(), previous.value());
}

// ---------------------------------------------------------------------------
// track.set_send
// ---------------------------------------------------------------------------

SetTrackSend::SetTrackSend(TrackId trackId, TrackId busId, double levelDb)
    : trackId_{trackId}
    , busId_{busId}
    , levelDb_{levelDb}
{
}

Result<std::unique_ptr<Command>> SetTrackSend::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto busId = trackIdAt(payload, "busId");
    if (!busId)
        return busId.error();

    auto level = payload.doubleAt("levelDb");
    if (!level)
        return level.error();

    return std::unique_ptr<Command>{new SetTrackSend{trackId.value(), busId.value(), level.value()}};
}

Value SetTrackSend::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}},
                          {"busId", Value{busId_.toString()}},
                          {"levelDb", Value{levelDb_}}});
}

Result<Value> SetTrackSend::apply(ProjectState& state) const
{
    const auto* strip = state.findStrip(trackId_);
    if (strip == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId_.toString());

    const auto* existing = strip->findSend(busId_);
    auto record = Value::object({{"trackId", Value{trackId_.toString()}},
                                 {"busId", Value{busId_.toString()}},
                                 {"existed", Value{existing != nullptr}},
                                 {"previousDb", Value{existing != nullptr ? existing->levelDb : 0.0}}});

    if (auto applied = state.setTrackSend(trackId_, busId_, levelDb_); !applied)
        return applied.error();

    return record;
}

Result<void> SetTrackSend::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto busId = trackIdAt(undoRecord, "busId");
    if (!busId)
        return busId.error();

    auto existed = undoRecord.boolAt("existed");
    if (!existed)
        return existed.error();

    // The gesture made the send: its undo takes it away, rather than leaving
    // a send at some default level nobody chose.
    if (!existed.value())
        return state.removeTrackSend(trackId.value(), busId.value());

    auto previous = undoRecord.doubleAt("previousDb");
    if (!previous)
        return previous.error();

    return state.setTrackSend(trackId.value(), busId.value(), previous.value());
}

bool SetTrackSend::canCoalesceWith(const Command& newer) const noexcept
{
    const auto* other = dynamic_cast<const SetTrackSend*>(&newer);
    return other != nullptr && other->trackId_ == trackId_ && other->busId_ == busId_;
}

// ---------------------------------------------------------------------------
// track.remove_send
// ---------------------------------------------------------------------------

RemoveTrackSend::RemoveTrackSend(TrackId trackId, TrackId busId)
    : trackId_{trackId}
    , busId_{busId}
{
}

Result<std::unique_ptr<Command>> RemoveTrackSend::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto busId = trackIdAt(payload, "busId");
    if (!busId)
        return busId.error();

    return std::unique_ptr<Command>{new RemoveTrackSend{trackId.value(), busId.value()}};
}

Value RemoveTrackSend::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"busId", Value{busId_.toString()}}});
}

Result<Value> RemoveTrackSend::apply(ProjectState& state) const
{
    auto index = state.sendIndex(trackId_, busId_);
    if (!index)
        return index.error();

    const auto level = state.findStrip(trackId_)->sends[index.value()].levelDb;
    if (auto removed = state.removeTrackSend(trackId_, busId_); !removed)
        return removed.error();

    return Value::object({{"trackId", Value{trackId_.toString()}},
                          {"busId", Value{busId_.toString()}},
                          {"levelDb", Value{level}},
                          {"index", Value{static_cast<std::int64_t>(index.value())}}});
}

Result<void> RemoveTrackSend::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto busId = trackIdAt(undoRecord, "busId");
    if (!busId)
        return busId.error();

    auto level = undoRecord.doubleAt("levelDb");
    if (!level)
        return level.error();

    auto index = undoRecord.intAt("index");
    if (!index)
        return index.error();
    if (index.value() < 0)
        return fail(ErrorCode::invalidPayload, "index is negative");

    Send send{};
    send.bus = busId.value();
    send.levelDb = level.value();
    return state.insertTrackSend(trackId.value(), send, static_cast<std::size_t>(index.value()));
}

// ---------------------------------------------------------------------------
// track.set_solo
// ---------------------------------------------------------------------------

SetTrackSolo::SetTrackSolo(TrackId trackId, bool soloed)
    : trackId_{trackId}
    , soloed_{soloed}
{
}

Result<std::unique_ptr<Command>> SetTrackSolo::fromPayload(const Value& payload)
{
    auto trackId = trackIdAt(payload, "trackId");
    if (!trackId)
        return trackId.error();

    auto soloed = payload.boolAt("soloed");
    if (!soloed)
        return soloed.error();

    return std::unique_ptr<Command>{new SetTrackSolo{trackId.value(), soloed.value()}};
}

Value SetTrackSolo::payload() const
{
    return Value::object({{"trackId", Value{trackId_.toString()}}, {"soloed", Value{soloed_}}});
}

Result<Value> SetTrackSolo::apply(ProjectState& state) const
{
    const auto* strip = state.findStrip(trackId_);
    if (strip == nullptr)
        return fail(ErrorCode::notFound, "no such track: " + trackId_.toString());

    const auto previous = strip->soloed;
    if (auto applied = state.setTrackSoloed(trackId_, soloed_); !applied)
        return applied.error();

    return Value::object({{"trackId", Value{trackId_.toString()}}, {"previousSoloed", Value{previous}}});
}

Result<void> SetTrackSolo::revert(ProjectState& state, const Value& undoRecord) const
{
    auto trackId = trackIdAt(undoRecord, "trackId");
    if (!trackId)
        return trackId.error();

    auto previous = undoRecord.boolAt("previousSoloed");
    if (!previous)
        return previous.error();

    return state.setTrackSoloed(trackId.value(), previous.value());
}

} // namespace daw::domain
