#include "daw/domain/commands/TransportCommands.h"

namespace daw::domain
{
namespace
{

Error neverReverted(std::string_view type)
{
    return fail(ErrorCode::invalidArgument, std::string{type} + " is transient and is never reverted");
}

} // namespace

// ---------------------------------------------------------------------------
// transport.play
// ---------------------------------------------------------------------------

Result<std::unique_ptr<Command>> TransportPlay::fromPayload(const Value& payload)
{
    static_cast<void>(payload); // no argument: starting is starting
    return std::unique_ptr<Command>{new TransportPlay{}};
}

Value TransportPlay::payload() const
{
    return Value::object({});
}

Result<Value> TransportPlay::apply(ProjectState& state) const
{
    auto applied = state.setPlaying(true);
    if (!applied)
        return applied.error();

    return Value{}; // transient: the bus keeps no undo record
}

Result<void> TransportPlay::revert(ProjectState& state, const Value& undoRecord) const
{
    static_cast<void>(state);
    static_cast<void>(undoRecord);
    return neverReverted(commandType);
}

// ---------------------------------------------------------------------------
// transport.stop
// ---------------------------------------------------------------------------

Result<std::unique_ptr<Command>> TransportStop::fromPayload(const Value& payload)
{
    static_cast<void>(payload);
    return std::unique_ptr<Command>{new TransportStop{}};
}

Value TransportStop::payload() const
{
    return Value::object({});
}

TransportSetLoop::TransportSetLoop(bool looping, double startBeats, double endBeats)
    : looping_{looping}
    , startBeats_{startBeats}
    , endBeats_{endBeats}
{
}

Result<std::unique_ptr<Command>> TransportSetLoop::fromPayload(const Value& payload)
{
    auto looping = payload.boolAt("looping");
    if (!looping)
        return looping.error();

    auto startBeats = payload.doubleAt("startBeats");
    if (!startBeats)
        return startBeats.error();

    auto endBeats = payload.doubleAt("endBeats");
    if (!endBeats)
        return endBeats.error();

    return std::unique_ptr<Command>{
        new TransportSetLoop{looping.value(), startBeats.value(), endBeats.value()}};
}

Value TransportSetLoop::payload() const
{
    return Value::object(
        {{"looping", Value{looping_}}, {"startBeats", Value{startBeats_}}, {"endBeats", Value{endBeats_}}});
}

Result<Value> TransportSetLoop::apply(ProjectState& state) const
{
    if (auto applied = state.setLoop(looping_, startBeats_, endBeats_); !applied)
        return applied.error();

    return Value{};
}

Result<void> TransportSetLoop::revert(ProjectState& state, const Value& undoRecord) const
{
    static_cast<void>(state);
    static_cast<void>(undoRecord);
    return fail(ErrorCode::invalidArgument, "transport.set_loop is transient and is never reverted");
}

Result<Value> TransportStop::apply(ProjectState& state) const
{
    auto applied = state.setPlaying(false);
    if (!applied)
        return applied.error();

    // Stopping returns the playhead to the start. This contradicts the comment
    // written in S2 ("stopping keeps the position, the way every DAW behaves"):
    // it is not what a beatmaker expects, where stop is how you go back to the
    // top of the pattern, and the rewind button exists for the other case --
    // going back to the start without stopping.
    if (auto moved = state.setPositionBeats(0.0); !moved)
        return moved.error();

    return Value{};
}

Result<void> TransportStop::revert(ProjectState& state, const Value& undoRecord) const
{
    static_cast<void>(state);
    static_cast<void>(undoRecord);
    return neverReverted(commandType);
}

// ---------------------------------------------------------------------------
// transport.set_mode
// ---------------------------------------------------------------------------

TransportSetMode::TransportSetMode(PlayMode mode, PatternId auditioned)
    : mode_{mode}
    , auditioned_{auditioned}
{
}

Result<std::unique_ptr<Command>> TransportSetMode::fromPayload(const Value& payload)
{
    auto mode = payload.stringAt("mode");
    if (!mode)
        return mode.error();

    PlayMode parsed{PlayMode::song};
    if (mode.value() == patternMode)
        parsed = PlayMode::pattern;
    else if (mode.value() != songMode)
        return fail(ErrorCode::invalidPayload, "mode is neither pattern nor song: " + mode.value());

    auto patternId = payload.stringAt("patternId");
    if (!patternId)
        return patternId.error();

    // An empty string names no pattern. It is spelled out rather than left
    // absent, so the payload always carries the same two keys.
    PatternId auditioned{};
    if (!patternId.value().empty())
    {
        auto id = PatternId::parse(patternId.value());
        if (!id)
            return fail(id.error().code, "patternId: " + id.error().message);
        auditioned = id.value();
    }

    return std::unique_ptr<Command>{new TransportSetMode{parsed, auditioned}};
}

Value TransportSetMode::payload() const
{
    return Value::object(
        {{"mode", Value{std::string{mode_ == PlayMode::pattern ? patternMode : songMode}}},
         {"patternId", Value{auditioned_.isNil() ? std::string{} : auditioned_.toString()}}});
}

Result<Value> TransportSetMode::apply(ProjectState& state) const
{
    if (auto applied = state.setPlayMode(mode_, auditioned_); !applied)
        return applied.error();

    if (auto moved = state.setPositionBeats(0.0); !moved)
        return moved.error();

    return Value{};
}

Result<void> TransportSetMode::revert(ProjectState& state, const Value& undoRecord) const
{
    static_cast<void>(state);
    static_cast<void>(undoRecord);
    return neverReverted(commandType);
}

// ---------------------------------------------------------------------------
// transport.set_position
// ---------------------------------------------------------------------------

TransportSetPosition::TransportSetPosition(double positionBeats)
    : positionBeats_{positionBeats}
{
}

Result<std::unique_ptr<Command>> TransportSetPosition::fromPayload(const Value& payload)
{
    auto positionBeats = payload.doubleAt("positionBeats");
    if (!positionBeats)
        return positionBeats.error();

    return std::unique_ptr<Command>{new TransportSetPosition{positionBeats.value()}};
}

Value TransportSetPosition::payload() const
{
    return Value::object({{"positionBeats", Value{positionBeats_}}});
}

Result<Value> TransportSetPosition::apply(ProjectState& state) const
{
    auto applied = state.setPositionBeats(positionBeats_);
    if (!applied)
        return applied.error();

    return Value{};
}

Result<void> TransportSetPosition::revert(ProjectState& state, const Value& undoRecord) const
{
    static_cast<void>(state);
    static_cast<void>(undoRecord);
    return neverReverted(commandType);
}

} // namespace daw::domain
