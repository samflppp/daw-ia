#pragma once

#include <cstdint>

namespace daw::domain
{

// What the bus does with a command once it has run.
enum class HistoryPolicy : std::uint8_t
{
    // The normal case: history entry, journal, undo.
    undoable,

    // Executed, validated and notified like any other command, but it leaves
    // no history entry and no journal entry. Reserved for the actions that are
    // not project state: starting playback is not something you undo, and
    // journalling it would make a replay start making noise.
    transient,
};

} // namespace daw::domain
