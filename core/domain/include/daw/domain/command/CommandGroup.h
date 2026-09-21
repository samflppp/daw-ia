#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <string>

namespace daw::domain
{

// Names the history entry several commands share.
//
// One request to the copilot can emit commands of different types — a track,
// then a plugin on it — and for whoever asked, that is one action: one line in
// the history, one Ctrl+Z. Coalescing cannot express it: it merges commands
// that are the same edit repeated, and these are not.
//
// The group is a property of the history, never a rewriting of the journal.
// Each command keeps its own row, its own identifier and its own payload; they
// merely carry the same group. That is what lets the journal stay append-only
// and a replay rebuild the same single entry.
//
// The label is what the panel shows. It is written by whoever opened the group
// — for the copilot, the sentence the user typed, shortened — because
// "track.add" followed by "plugin.insert" says nothing about what was asked.
struct GroupRef
{
    static constexpr std::size_t maxLabelLength = 120;

    GroupId id{};
    std::string label;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<GroupRef> fromValue(const Value& value);

    friend bool operator==(const GroupRef& lhs, const GroupRef& rhs);
    friend bool operator!=(const GroupRef& lhs, const GroupRef& rhs) { return !(lhs == rhs); }
};

} // namespace daw::domain
