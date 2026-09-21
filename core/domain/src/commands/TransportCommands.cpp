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
