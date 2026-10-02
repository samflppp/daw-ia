#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/HistoryPolicy.h"
#include "daw/domain/command/Reach.h"
#include "daw/domain/project/ProjectState.h"

#include <cstdint>
#include <string_view>

namespace daw::domain
{

// Every mutation of the project goes through a Command. No exception: it is
// what gives undo, versioning and MCP control one single mechanism.
//
// Two separate serialized forms, and the split matters:
//
//   payload()  the intention, and only the intention. A command rebuilt from
//              its payload alone is executable: that is what makes replay,
//              JSON-RPC and MCP work. Therefore a command never invents an
//              identifier inside apply() — the caller puts it in the payload,
//              or replaying the same payload would produce a different state.
//
//   undoRecord the part that only exists once the command has run: the state it
//              overwrote. apply() returns it, the bus keeps it next to the
//              command, revert() consumes it. It is a Value too, so a later
//              week can persist a whole history to SQLite.
class Command
{
public:
    Command() = default;
    virtual ~Command() = default;

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;

    // Stable wire name, e.g. "clip.create_midi". Matches the command types
    // listed in the workspace manifests.
    [[nodiscard]] virtual std::string_view type() const noexcept = 0;

    [[nodiscard]] virtual Value payload() const = 0;

    [[nodiscard]] virtual HistoryPolicy historyPolicy() const noexcept { return HistoryPolicy::undoable; }

    // What it changes, for the screens that redraw only that (see Reach.h).
    [[nodiscard]] virtual Reach reach() const noexcept { return Reach::anything; }

    // Validates first, mutates second: on failure the state is untouched, and
    // the bus records nothing.
    [[nodiscard]] virtual Result<Value> apply(ProjectState& state) const = 0;

    [[nodiscard]] virtual Result<void> revert(ProjectState& state, const Value& undoRecord) const = 0;

    // Coalescing, half one: the type says whether it is *able* to absorb the
    // newer command (same target, continuous parameter). The caller says
    // whether it *wants* to, by passing a gesture to CommandBus::execute.
    // Both halves must agree, so nothing merges by accident.
    [[nodiscard]] virtual bool canCoalesceWith(const Command& newer) const noexcept
    {
        static_cast<void>(newer);
        return false;
    }
};

} // namespace daw::domain
