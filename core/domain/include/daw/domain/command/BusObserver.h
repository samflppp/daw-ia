#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Timestamp.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/HistoryPolicy.h"
#include "daw/domain/command/Provenance.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace daw::domain
{

// What the bus reports after a successful operation. The identifier is the one
// of the *history entry*, not of the last command: when a gesture coalesces
// sixty fader moves, the sixty receipts carry the identifier of the first one.
struct Receipt
{
    CommandId id{};
    std::string type;
    Timestamp at{};
    std::optional<GestureId> gesture;
    bool coalesced{false}; // merged into the entry on top, no new entry created
    std::size_t undoDepth{0};
    std::size_t redoDepth{0};

    // Who asked for *this operation*: the author of the command when it is
    // executed or coalesced, the actor who asked for the move when it is an
    // undo or a redo. The author of an entry is never rewritten by an undo:
    // it stays in the execute record of the journal.
    Provenance origin{};

    // Transient commands are executed and notified like the others, and the
    // journal must skip them: a project that replayed a transport.play would
    // start making noise the moment it was reopened.
    HistoryPolicy policy{HistoryPolicy::undoable};

    // The intention, as the journal will store it. Filled for an execution or
    // a coalescing — for a coalescing it is the merged payload, the one that
    // must be replayed — and left null for an undo or a redo, which replay
    // nothing and only move a pointer in the history.
    Value payload{};
};

// Observers are notified after the state has changed and before execute(),
// undo() or redo() returns. They must not mutate the project: any call back
// into the bus from here fails with ErrorCode::reentrantCall.
class BusObserver
{
public:
    BusObserver() = default;
    virtual ~BusObserver() = default;

    BusObserver(const BusObserver&) = delete;
    BusObserver& operator=(const BusObserver&) = delete;
    BusObserver(BusObserver&&) = delete;
    BusObserver& operator=(BusObserver&&) = delete;

    virtual void onExecuted(const Receipt& receipt) { static_cast<void>(receipt); }
    virtual void onCoalesced(const Receipt& receipt) { static_cast<void>(receipt); }
    virtual void onUndone(const Receipt& receipt) { static_cast<void>(receipt); }
    virtual void onRedone(const Receipt& receipt) { static_cast<void>(receipt); }
    virtual void onHistoryTruncated(std::size_t droppedEntries) { static_cast<void>(droppedEntries); }
};

struct ObserverToken
{
    std::uint64_t value{0};

    friend bool operator==(ObserverToken lhs, ObserverToken rhs) noexcept { return lhs.value == rhs.value; }
};

} // namespace daw::domain
