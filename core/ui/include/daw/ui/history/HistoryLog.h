#pragma once

#include "daw/domain/command/BusObserver.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::ui
{

// What the history panel shows, built from what the bus reports.
//
// The bus owns the undo and redo stacks and gives no reading of them beyond
// their depth, which is deliberate: an interface that could walk the stacks
// would end up holding commands it must not touch. So the panel is told the
// same thing every other observer is told — a receipt per operation — and
// keeps its own list from it.
//
// This is a mirror, never a second truth: nothing here is replayed, saved or
// undone, and losing it would cost a display, not an edit.
//
// No JUCE here on purpose. Which entry is where, what a coalescing does to the
// list, and what an execution does to the redo branch are the questions with a
// right answer, so they are tested in the fast suite.
class HistoryLog : public domain::BusObserver
{
public:
    struct Entry
    {
        domain::CommandId id{};
        std::string type;
        domain::Timestamp at{};

        // The author of the entry, taken from the command that created it. An
        // undo never rewrites it: the copilot that undoes a user's edit has
        // not become the author of that edit.
        domain::Actor actor{domain::Actor::user};

        // How many commands the entry holds. One, unless a gesture merged a
        // sweep into it, or a group made several commands one action.
        std::size_t merged{1};

        // The group this entry was built from, when it was built from one.
        // Its label is what the panel shows instead of the type of whichever
        // command happened to come first: "track.add" says nothing about a
        // request that asked for a track and a plugin on it.
        std::optional<domain::GroupRef> group;

        // What a reader of the panel should see on this line.
        [[nodiscard]] std::string_view label() const noexcept;
    };

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

    // Entries before the cursor are applied; the ones after it are undone and
    // can be redone. Equal to the bus's undo depth, by construction.
    [[nodiscard]] std::size_t cursor() const noexcept { return cursor_; }

    void clear() noexcept;

    // The label a human reads instead of "track.set_muted". An unknown type is
    // returned as it is: a command this build does not know about is better
    // shown by its name than hidden behind "Modification".
    [[nodiscard]] static std::string_view describe(std::string_view type) noexcept;

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;
    void onHistoryTruncated(std::size_t droppedEntries) override;

private:
    std::vector<Entry> entries_;
    std::size_t cursor_{0};
};

} // namespace daw::ui
