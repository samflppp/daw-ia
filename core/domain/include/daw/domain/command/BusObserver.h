#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Timestamp.h"

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
