#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::domain
{

struct ExecuteOptions
{
    // Set it to the identifier returned by beginGesture() to allow the command
    // to merge with the previous one of the same gesture. Left empty, the
    // command always creates its own history entry.
    std::optional<GestureId> gesture;
};

struct BusLimits
{
    // Beyond this depth the oldest entry is dropped and observers are told.
    std::size_t maxUndoDepth{512};
};

// Executes commands, keeps the undo and redo stacks, notifies observers, and
// produces the journal the versioning layer will persist.
//
// The bus owns no state of its own about the project: it holds a reference to
// a ProjectState and only touches it through commands.
class CommandBus
{
public:
    CommandBus(ProjectState& state, const CommandRegistry& registry, BusLimits limits = {});

    CommandBus(const CommandBus&) = delete;
    CommandBus& operator=(const CommandBus&) = delete;
    CommandBus(CommandBus&&) = delete;
    CommandBus& operator=(CommandBus&&) = delete;

    // --- execution ---------------------------------------------------------
    Result<Receipt> execute(std::unique_ptr<Command> command, ExecuteOptions options = {});

    // Replay path: rebuilds the command from the registry and keeps the
    // identifier and the date of the envelope, so a replayed history is the
    // same history. Never coalesces: an envelope already is the merged form.
    Result<Receipt> executeSerialized(const Value& envelope);

    // --- history -----------------------------------------------------------
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    Result<Receipt> undo();
    Result<Receipt> redo();
    [[nodiscard]] std::size_t undoDepth() const noexcept;
    [[nodiscard]] std::size_t redoDepth() const noexcept;
    void clearHistory() noexcept;

    // --- gestures ----------------------------------------------------------
    // One gesture at a time. Opening a second one closes the first.
    [[nodiscard]] GestureId beginGesture(std::string_view label);
    Result<void> endGesture(GestureId gesture);
    [[nodiscard]] std::optional<GestureId> openGesture() const noexcept;
    [[nodiscard]] std::string_view openGestureLabel() const noexcept;

    // --- observers ---------------------------------------------------------
    ObserverToken addObserver(BusObserver& observer);
    void removeObserver(ObserverToken token) noexcept;

    // --- journal -----------------------------------------------------------
    // The envelopes that build the current state, oldest first, after merging.
    // Replaying them on an empty project reproduces the state exactly.
    [[nodiscard]] std::vector<Value> journal() const;

private:
    struct Entry
    {
        CommandId id{};
        Timestamp at{};
        std::optional<GestureId> gesture;
        std::unique_ptr<Command> command;
        Value undoRecord;
    };

    struct Registration
    {
        ObserverToken token;
        BusObserver* observer{nullptr};
    };

    Result<Receipt> executeEntry(std::unique_ptr<Command> command,
                                 CommandId id,
                                 Timestamp at,
                                 std::optional<GestureId> gesture,
                                 bool allowCoalescing);

    [[nodiscard]] Receipt receiptFor(const Entry& entry, bool coalesced) const;
    void notify(void (BusObserver::*callback)(const Receipt&), const Receipt& receipt);
    void notifyTruncated(std::size_t dropped);

    ProjectState& state_;
    const CommandRegistry& registry_;
    BusLimits limits_;

    std::vector<Entry> undoStack_;
    std::vector<Entry> redoStack_;
    std::optional<GestureId> openGesture_;
    std::string openGestureLabel_;
    std::vector<Registration> observers_;
    std::uint64_t nextObserverToken_{1};
    bool mutating_{false};
};

} // namespace daw::domain
