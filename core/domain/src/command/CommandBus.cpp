#include "daw/domain/command/CommandBus.h"

#include <algorithm>
#include <utility>

namespace daw::domain
{
namespace
{

// Holds the "a mutation is in progress" flag for the whole operation,
// notifications included, so an observer cannot re-enter the bus.
class MutationScope
{
public:
    explicit MutationScope(bool& flag) noexcept
        : flag_{flag}
    {
        flag_ = true;
    }

    ~MutationScope() { flag_ = false; }

    MutationScope(const MutationScope&) = delete;
    MutationScope& operator=(const MutationScope&) = delete;
    MutationScope(MutationScope&&) = delete;
    MutationScope& operator=(MutationScope&&) = delete;

private:
    bool& flag_;
};

Error reentrant()
{
    return fail(ErrorCode::reentrantCall, "the bus is already applying a command");
}

} // namespace

CommandBus::CommandBus(ProjectState& state, const CommandRegistry& registry, BusLimits limits)
    : state_{state}
    , registry_{registry}
    , limits_{limits}
{
    if (limits_.maxUndoDepth == 0)
        limits_.maxUndoDepth = 1;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

Result<Receipt> CommandBus::execute(std::unique_ptr<Command> command, ExecuteOptions options)
{
    if (mutating_)
        return reentrant();

    if (command == nullptr)
        return fail(ErrorCode::invalidArgument, "no command given");

    if (options.gesture.has_value())
    {
        if (!openGesture_.has_value() || *openGesture_ != *options.gesture)
            return fail(ErrorCode::gestureClosed,
                        "gesture " + options.gesture->toString() + " is not the open one");
    }

    return executeEntry(std::move(command),
                        CommandId::generate(),
                        Timestamp::now(),
                        options.gesture,
                        options.gesture.has_value());
}

Result<Receipt> CommandBus::executeSerialized(const Value& envelope)
{
    if (mutating_)
        return reentrant();

    auto parsed = CommandEnvelope::fromValue(envelope);
    if (!parsed)
        return parsed.error();

    auto command = registry_.create(parsed.value().type, parsed.value().payload);
    if (!command)
        return command.error();

    // Identity and date come from the envelope: a replayed history is the same
    // history, not a new one. Coalescing is off — the envelope is already the
    // merged form of its gesture.
    return executeEntry(
        std::move(command).value(), parsed.value().id, parsed.value().at, parsed.value().gesture, false);
}

Result<Receipt> CommandBus::executeEntry(std::unique_ptr<Command> command,
                                         CommandId id,
                                         Timestamp at,
                                         std::optional<GestureId> gesture,
                                         bool allowCoalescing)
{
    const MutationScope scope{mutating_};

    // Both halves must agree: the caller asked for this gesture, and the type
    // on top of the history accepts to absorb the newcomer.
    const bool coalescing = allowCoalescing && gesture.has_value() && !undoStack_.empty() &&
                            undoStack_.back().gesture == gesture &&
                            undoStack_.back().command->canCoalesceWith(*command);

    auto applied = command->apply(state_);
    if (!applied)
        return applied.error();

    if (coalescing)
    {
        // Keep the undo record of the *first* command of the gesture: undoing
        // must go back to before the gesture, not one frame back.
        auto& entry = undoStack_.back();
        entry.command = std::move(command);

        const auto receipt = receiptFor(entry, true);
        notify(&BusObserver::onCoalesced, receipt);
        return receipt;
    }

    redoStack_.clear();

    Entry entry{};
    entry.id = id;
    entry.at = at;
    entry.gesture = gesture;
    entry.command = std::move(command);
    entry.undoRecord = std::move(applied).value();
    undoStack_.push_back(std::move(entry));

    std::size_t dropped = 0;
    while (undoStack_.size() > limits_.maxUndoDepth)
    {
        undoStack_.erase(undoStack_.begin());
        ++dropped;
    }

    const auto receipt = receiptFor(undoStack_.back(), false);
    notify(&BusObserver::onExecuted, receipt);
    if (dropped > 0)
        notifyTruncated(dropped);

    return receipt;
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

bool CommandBus::canUndo() const noexcept
{
    return !undoStack_.empty();
}

bool CommandBus::canRedo() const noexcept
{
    return !redoStack_.empty();
}

std::size_t CommandBus::undoDepth() const noexcept
{
    return undoStack_.size();
}

std::size_t CommandBus::redoDepth() const noexcept
{
    return redoStack_.size();
}

void CommandBus::clearHistory() noexcept
{
    undoStack_.clear();
    redoStack_.clear();
}

Result<Receipt> CommandBus::undo()
{
    if (mutating_)
        return reentrant();

    if (undoStack_.empty())
        return fail(ErrorCode::nothingToUndo, "the undo stack is empty");

    const MutationScope scope{mutating_};

    auto& entry = undoStack_.back();
    auto reverted = entry.command->revert(state_, entry.undoRecord);
    if (!reverted)
        return reverted.error();

    // A gesture cannot survive an undo: the entry it was feeding is gone.
    openGesture_.reset();
    openGestureLabel_.clear();

    redoStack_.push_back(std::move(undoStack_.back()));
    undoStack_.pop_back();

    const auto receipt = receiptFor(redoStack_.back(), false);
    notify(&BusObserver::onUndone, receipt);
    return receipt;
}

Result<Receipt> CommandBus::redo()
{
    if (mutating_)
        return reentrant();

    if (redoStack_.empty())
        return fail(ErrorCode::nothingToRedo, "the redo stack is empty");

    const MutationScope scope{mutating_};

    auto& entry = redoStack_.back();
    auto applied = entry.command->apply(state_);
    if (!applied)
        return applied.error();

    entry.undoRecord = std::move(applied).value();
    undoStack_.push_back(std::move(redoStack_.back()));
    redoStack_.pop_back();

    const auto receipt = receiptFor(undoStack_.back(), false);
    notify(&BusObserver::onRedone, receipt);
    return receipt;
}

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------

GestureId CommandBus::beginGesture(std::string_view label)
{
    openGesture_ = GestureId::generate();
    openGestureLabel_ = std::string{label};
    return *openGesture_;
}

Result<void> CommandBus::endGesture(GestureId gesture)
{
    if (!openGesture_.has_value() || *openGesture_ != gesture)
        return fail(ErrorCode::gestureClosed, "gesture " + gesture.toString() + " is not the open one");

    openGesture_.reset();
    openGestureLabel_.clear();
    return {};
}

std::optional<GestureId> CommandBus::openGesture() const noexcept
{
    return openGesture_;
}

std::string_view CommandBus::openGestureLabel() const noexcept
{
    return openGestureLabel_;
}

// ---------------------------------------------------------------------------
// Observers
// ---------------------------------------------------------------------------

ObserverToken CommandBus::addObserver(BusObserver& observer)
{
    const ObserverToken token{nextObserverToken_++};
    observers_.push_back(Registration{token, &observer});
    return token;
}

void CommandBus::removeObserver(ObserverToken token) noexcept
{
    const auto position =
        std::find_if(observers_.begin(),
                     observers_.end(),
                     [token](const Registration& registration) { return registration.token == token; });
    if (position != observers_.end())
        observers_.erase(position);
}

void CommandBus::notify(void (BusObserver::*callback)(const Receipt&), const Receipt& receipt)
{
    // Iterating over a copy keeps the loop valid if an observer unregisters
    // itself; the token check skips the ones that just left.
    const auto snapshot = observers_;
    for (const auto& registration : snapshot)
    {
        const auto stillRegistered = std::any_of(observers_.begin(),
                                                 observers_.end(),
                                                 [&registration](const Registration& current)
                                                 { return current.token == registration.token; });
        if (stillRegistered)
            (registration.observer->*callback)(receipt);
    }
}

void CommandBus::notifyTruncated(std::size_t dropped)
{
    const auto snapshot = observers_;
    for (const auto& registration : snapshot)
    {
        const auto stillRegistered = std::any_of(observers_.begin(),
                                                 observers_.end(),
                                                 [&registration](const Registration& current)
                                                 { return current.token == registration.token; });
        if (stillRegistered)
            registration.observer->onHistoryTruncated(dropped);
    }
}

// ---------------------------------------------------------------------------
// Journal
// ---------------------------------------------------------------------------

std::vector<Value> CommandBus::journal() const
{
    std::vector<Value> envelopes;
    envelopes.reserve(undoStack_.size());

    for (const auto& entry : undoStack_)
    {
        CommandEnvelope envelope{};
        envelope.id = entry.id;
        envelope.type = std::string{entry.command->type()};
        envelope.at = entry.at;
        envelope.gesture = entry.gesture;
        envelope.payload = entry.command->payload();
        envelopes.push_back(envelope.toValue());
    }

    return envelopes;
}

Receipt CommandBus::receiptFor(const Entry& entry, bool coalesced) const
{
    Receipt receipt{};
    receipt.id = entry.id;
    receipt.type = std::string{entry.command->type()};
    receipt.at = entry.at;
    receipt.gesture = entry.gesture;
    receipt.coalesced = coalesced;
    receipt.undoDepth = undoStack_.size();
    receipt.redoDepth = redoStack_.size();
    return receipt;
}

} // namespace daw::domain
