#include "daw/domain/command/CommandBus.h"

#include <algorithm>
#include <cassert>
#include <sstream>
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

std::string describeThread(std::thread::id id)
{
    std::ostringstream text;
    text << id;
    return text.str();
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
    auto type = command != nullptr ? std::string{command->type()} : std::string{};
    auto result = executeChecked(std::move(command), std::move(options));
    if (!result)
        reportRefusal("execute", std::move(type), result.error());
    return result;
}

Result<Receipt> CommandBus::executeChecked(std::unique_ptr<Command> command, ExecuteOptions options)
{
    if (auto owned = checkThread(); !owned)
        return owned.error();

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

    if (auto valid = options.origin.validate(); !valid)
        return valid.error();

    return executeEntry(std::move(command),
                        CommandId::generate(),
                        Timestamp::now(),
                        options.gesture,
                        options.origin,
                        options.gesture.has_value());
}

Result<Receipt> CommandBus::executeSerialized(const Value& envelope)
{
    if (auto owned = checkThread(); !owned)
        return owned.error();

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
    return executeEntry(std::move(command).value(),
                        parsed.value().id,
                        parsed.value().at,
                        parsed.value().gesture,
                        parsed.value().origin,
                        false);
}

Result<Receipt> CommandBus::executeEntry(std::unique_ptr<Command> command,
                                         CommandId id,
                                         Timestamp at,
                                         std::optional<GestureId> gesture,
                                         Provenance origin,
                                         bool allowCoalescing)
{
    const MutationScope scope{mutating_};

    // Transient commands (the transport) never touch the history: no entry, no
    // journal, and the redo stack is left alone because nothing about the past
    // has changed. They are still validated and still notified.
    if (command->historyPolicy() == HistoryPolicy::transient)
    {
        auto appliedTransient = command->apply(state_);
        if (!appliedTransient)
            return appliedTransient.error();

        Receipt receipt{};
        receipt.id = id;
        receipt.type = std::string{command->type()};
        receipt.at = at;
        receipt.gesture = gesture;
        receipt.coalesced = false;
        receipt.undoDepth = undoStack_.size();
        receipt.redoDepth = redoStack_.size();
        receipt.origin = origin;
        receipt.policy = HistoryPolicy::transient;
        receipt.reach = command->reach();
        receipt.payload = command->payload();

        notify(&BusObserver::onExecuted, receipt);
        return receipt;
    }

    // Both halves must agree: the caller asked for this gesture, and the type
    // on top of the history accepts to absorb the newcomer.
    // Same target, same gesture -- and same origin. A copilot does not graft a
    // parameter onto a gesture a human started: merging the two would make one
    // history entry whose author is a lie, and undo would give it all back to
    // whoever the first command happened to belong to.
    //
    // A grouped entry absorbs nothing. Its commands are several by definition,
    // and merging a fader move into "add a Bass track and put Vital on it"
    // would make one line that undoes two unrelated things.
    const bool coalescing = allowCoalescing && gesture.has_value() && !undoStack_.empty() &&
                            undoStack_.back().steps.size() == 1 && !undoStack_.back().group.has_value() &&
                            undoStack_.back().gesture == gesture && undoStack_.back().origin == origin &&
                            undoStack_.back().first().command->canCoalesceWith(*command);

    auto applied = command->apply(state_);
    if (!applied)
        return applied.error();

    if (coalescing)
    {
        // Keep the undo record of the *first* command of the gesture: undoing
        // must go back to before the gesture, not one frame back.
        auto& entry = undoStack_.back();
        entry.steps.front().command = std::move(command);

        const auto receipt = receiptFor(entry, entry.first(), true);
        notify(&BusObserver::onCoalesced, receipt);
        return receipt;
    }

    redoStack_.clear();

    Step step{};
    step.id = id;
    step.at = at;
    step.command = std::move(command);
    step.undoRecord = std::move(applied).value();

    Entry entry{};
    entry.gesture = gesture;
    entry.origin = origin;
    entry.steps.push_back(std::move(step));
    undoStack_.push_back(std::move(entry));

    const auto dropped = trimToLimit();

    const auto receipt = receiptFor(undoStack_.back(), undoStack_.back().first(), false);
    notify(&BusObserver::onExecuted, receipt);
    if (dropped > 0)
        notifyTruncated(dropped);

    return receipt;
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

Result<std::vector<Receipt>> CommandBus::executeGroup(std::vector<std::unique_ptr<Command>> commands,
                                                      GroupOptions options)
{
    std::string types;
    for (const auto& command : commands)
        types +=
            (types.empty() ? "" : ",") + (command != nullptr ? std::string{command->type()} : std::string{});
    auto result = executeGroupChecked(std::move(commands), std::move(options));
    if (!result)
        reportRefusal("group", std::move(types), result.error());
    return result;
}

Result<std::vector<Receipt>> CommandBus::executeGroupChecked(std::vector<std::unique_ptr<Command>> commands,
                                                             GroupOptions options)
{
    if (auto owned = checkThread(); !owned)
        return owned.error();

    if (mutating_)
        return reentrant();

    if (auto valid = options.origin.validate(); !valid)
        return valid.error();

    GroupRef group{};
    group.id = GroupId::generate();
    group.label = std::move(options.label);
    if (auto valid = group.validate(); !valid)
        return valid.error();

    std::vector<GroupCommand> steps;
    steps.reserve(commands.size());
    for (auto& command : commands)
    {
        GroupCommand step{};
        step.id = CommandId::generate();
        step.at = Timestamp::now();
        step.command = std::move(command);
        steps.push_back(std::move(step));
    }

    return executeGroupEntry(std::move(steps), std::move(group), options.origin);
}

Result<std::vector<Receipt>> CommandBus::executeSerializedGroup(const std::vector<Value>& envelopes)
{
    if (auto owned = checkThread(); !owned)
        return owned.error();

    if (mutating_)
        return reentrant();

    if (envelopes.empty())
        return fail(ErrorCode::invalidArgument, "a group holds at least one command");

    std::optional<GroupRef> group;
    Provenance origin{};
    std::vector<GroupCommand> steps;
    steps.reserve(envelopes.size());

    for (const auto& value : envelopes)
    {
        auto parsed = CommandEnvelope::fromValue(value);
        if (!parsed)
            return parsed.error();

        auto& envelope = parsed.value();
        if (!envelope.group.has_value())
            return fail(ErrorCode::invalidPayload, "an envelope of the group names no group");

        if (!group.has_value())
        {
            group = envelope.group;
            origin = envelope.origin;
        }
        else if (*group != *envelope.group)
        {
            return fail(ErrorCode::invalidPayload, "the envelopes name two different groups");
        }

        auto command = registry_.create(envelope.type, envelope.payload);
        if (!command)
            return command.error();

        GroupCommand step{};
        step.id = envelope.id;
        step.at = envelope.at;
        step.command = std::move(command).value();
        steps.push_back(std::move(step));
    }

    return executeGroupEntry(std::move(steps), *group, origin);
}

Result<std::vector<Receipt>>
CommandBus::executeGroupEntry(std::vector<GroupCommand> commands, GroupRef group, Provenance origin)
{
    if (commands.empty())
        return fail(ErrorCode::invalidArgument, "a group holds at least one command");

    for (const auto& command : commands)
    {
        if (command.command == nullptr)
            return fail(ErrorCode::invalidArgument, "no command given");
    }

    const MutationScope scope{mutating_};

    Entry entry{};
    entry.group = group;
    entry.origin = origin;

    std::vector<Receipt> receipts;
    receipts.reserve(commands.size());

    // What a failure gives back, in the reverse order of what was applied. The
    // transient commands are not in here on purpose: they carry no undo record
    // and reverting a transport would invent a position nobody asked for.
    const auto rollback = [this, &entry]()
    {
        for (auto step = entry.steps.rbegin(); step != entry.steps.rend(); ++step)
            static_cast<void>(step->command->revert(state_, step->undoRecord));

        entry.steps.clear();
    };

    for (auto& command : commands)
    {
        const bool transient = command.command->historyPolicy() == HistoryPolicy::transient;

        auto applied = command.command->apply(state_);
        if (!applied)
        {
            rollback();
            return applied.error();
        }

        Receipt receipt{};
        receipt.id = command.id;
        receipt.type = std::string{command.command->type()};
        receipt.at = command.at;
        receipt.group = group;
        receipt.coalesced = false;
        receipt.origin = origin;
        receipt.policy = command.command->historyPolicy();
        receipt.reach = command.command->reach();
        receipt.payload = command.command->payload();
        receipts.push_back(std::move(receipt));

        if (transient)
            continue;

        Step step{};
        step.id = command.id;
        step.at = command.at;
        step.command = std::move(command.command);
        step.undoRecord = std::move(applied).value();
        entry.steps.push_back(std::move(step));
    }

    std::size_t dropped = 0;

    // A group of nothing but transient commands changed no project state, so
    // it leaves no entry and does not kill the redo branch: asking for a loop
    // must not cost the user the redo they still had.
    if (!entry.steps.empty())
    {
        redoStack_.clear();
        undoStack_.push_back(std::move(entry));
        dropped = trimToLimit();
    }

    for (auto& receipt : receipts)
    {
        receipt.undoDepth = undoStack_.size();
        receipt.redoDepth = redoStack_.size();
        notify(&BusObserver::onExecuted, receipt);
    }

    if (dropped > 0)
        notifyTruncated(dropped);

    return receipts;
}

std::size_t CommandBus::trimToLimit()
{
    std::size_t dropped = 0;
    while (undoStack_.size() > limits_.maxUndoDepth)
    {
        undoStack_.erase(undoStack_.begin());
        ++dropped;
    }

    return dropped;
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
    assert(checkThread().ok() && "clearHistory called from a thread that does not own the bus");

    undoStack_.clear();
    redoStack_.clear();
}

Result<Receipt> CommandBus::undo(Provenance by)
{
    auto result = undoChecked(std::move(by));
    if (!result)
        reportRefusal("undo", {}, result.error());
    return result;
}

Result<Receipt> CommandBus::undoChecked(Provenance by)
{
    if (auto owned = checkThread(); !owned)
        return owned.error();

    if (mutating_)
        return reentrant();

    if (undoStack_.empty())
        return fail(ErrorCode::nothingToUndo, "the undo stack is empty");

    const MutationScope scope{mutating_};

    auto& entry = undoStack_.back();

    // The steps of a group go back in the reverse order they were applied: the
    // plugin leaves the track before the track leaves the project.
    //
    // A revert that fails in the middle is the one case this bus cannot make
    // clean by itself, because the earlier steps are already back. It stops
    // there and says so: the entry stays on the undo stack, so the user can
    // ask again once whatever refused has been dealt with.
    for (auto step = entry.steps.rbegin(); step != entry.steps.rend(); ++step)
    {
        if (auto reverted = step->command->revert(state_, step->undoRecord); !reverted)
            return reverted.error();
    }

    // A gesture cannot survive an undo: the entry it was feeding is gone.
    openGesture_.reset();
    openGestureLabel_.clear();

    redoStack_.push_back(std::move(undoStack_.back()));
    undoStack_.pop_back();

    const auto receipt = moveReceiptFor(redoStack_.back(), by);
    notify(&BusObserver::onUndone, receipt);
    return receipt;
}

Result<Receipt> CommandBus::redo(Provenance by)
{
    auto result = redoChecked(std::move(by));
    if (!result)
        reportRefusal("redo", {}, result.error());
    return result;
}

Result<Receipt> CommandBus::redoChecked(Provenance by)
{
    if (auto owned = checkThread(); !owned)
        return owned.error();

    if (mutating_)
        return reentrant();

    if (redoStack_.empty())
        return fail(ErrorCode::nothingToRedo, "the redo stack is empty");

    const MutationScope scope{mutating_};

    auto& entry = redoStack_.back();

    // Forward order this time, and every step is applied before any of them is
    // kept: the undo records have to describe states that really existed.
    std::vector<Value> undoRecords;
    undoRecords.reserve(entry.steps.size());

    for (auto& step : entry.steps)
    {
        auto applied = step.command->apply(state_);
        if (!applied)
        {
            // Give back what this redo already applied, so a refusal in the
            // middle leaves the project where the user left it.
            for (std::size_t done = undoRecords.size(); done > 0; --done)
            {
                static_cast<void>(entry.steps[done - 1].command->revert(state_, undoRecords[done - 1]));
            }

            return applied.error();
        }

        undoRecords.push_back(std::move(applied).value());
    }

    for (std::size_t index = 0; index < entry.steps.size(); ++index)
        entry.steps[index].undoRecord = std::move(undoRecords[index]);
    undoStack_.push_back(std::move(redoStack_.back()));
    redoStack_.pop_back();

    const auto receipt = moveReceiptFor(undoStack_.back(), by);
    notify(&BusObserver::onRedone, receipt);
    return receipt;
}

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------

GestureId CommandBus::beginGesture(std::string_view label)
{
    assert(checkThread().ok() && "beginGesture called from a thread that does not own the bus");

    openGesture_ = GestureId::generate();
    openGestureLabel_ = std::string{label};
    return *openGesture_;
}

Result<void> CommandBus::endGesture(GestureId gesture)
{
    if (auto owned = checkThread(); !owned)
        return owned.error();

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
    assert(checkThread().ok() && "addObserver called from a thread that does not own the bus");

    const ObserverToken token{nextObserverToken_++};
    observers_.push_back(Registration{token, &observer});
    return token;
}

void CommandBus::removeObserver(ObserverToken token) noexcept
{
    assert(checkThread().ok() && "removeObserver called from a thread that does not own the bus");

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

    // One envelope per command, never one per entry: a group is several
    // commands that share a history entry, and a journal that wrote one row
    // for the lot would lose the payloads it needs to replay them.
    for (const auto& entry : undoStack_)
    {
        for (const auto& step : entry.steps)
        {
            CommandEnvelope envelope{};
            envelope.id = step.id;
            envelope.type = std::string{step.command->type()};
            envelope.at = step.at;
            envelope.gesture = entry.gesture;
            envelope.group = entry.group;
            envelope.origin = entry.origin;
            envelope.payload = step.command->payload();
            envelopes.push_back(envelope.toValue());
        }
    }

    return envelopes;
}

void CommandBus::setRefusalListener(std::function<void(const Refusal&)> listener)
{
    assert(std::this_thread::get_id() == owningThread_);
    refusalListener_ = std::move(listener);
}

void CommandBus::reportRefusal(std::string_view operation, std::string commandType, const Error& error) const
{
    if (!refusalListener_)
        return;

    Refusal refusal{};
    refusal.operation = std::string{operation};
    refusal.commandType = std::move(commandType);
    refusal.error = error;
    refusal.caller = std::this_thread::get_id();
    refusal.owner = owningThread_;
    refusal.mutating = mutating_;
    refusal.openGesture = openGestureLabel_;
    refusal.undoDepth = undoStack_.size();
    refusal.redoDepth = redoStack_.size();
    refusalListener_(refusal);
}

Result<void> CommandBus::checkThread() const
{
    if (std::this_thread::get_id() == owningThread_)
        return {};

    return fail(ErrorCode::wrongThread,
                "the bus belongs to thread " + describeThread(owningThread_) + ", called from " +
                    describeThread(std::this_thread::get_id()));
}

std::thread::id CommandBus::owningThread() const noexcept
{
    return owningThread_;
}

Result<void> CommandBus::rebindToCurrentThread()
{
    if (mutating_)
        return reentrant();

    if (auto owned = checkThread(); !owned)
        return owned.error();

    owningThread_ = std::this_thread::get_id();
    return {};
}

Receipt CommandBus::receiptFor(const Entry& entry, const Step& step, bool coalesced) const
{
    Receipt receipt{};
    receipt.id = step.id;
    receipt.type = std::string{step.command->type()};
    receipt.at = step.at;
    receipt.gesture = entry.gesture;
    receipt.group = entry.group;
    receipt.coalesced = coalesced;
    receipt.undoDepth = undoStack_.size();
    receipt.redoDepth = redoStack_.size();
    receipt.origin = entry.origin;
    receipt.reach = step.command->reach();
    receipt.payload = step.command->payload();
    return receipt;
}

Receipt CommandBus::moveReceiptFor(const Entry& entry, Provenance by) const
{
    // An undo or a redo replays nothing, so it carries no payload; and it
    // reports the actor who asked for the move, not the author of the entry.
    // An entry is the unit that moves, so the receipt names the entry: the
    // identifier of its first command, and the group when it has one.
    Receipt receipt{};
    receipt.id = entry.first().id;
    receipt.type = std::string{entry.first().command->type()};
    receipt.at = entry.first().at;
    receipt.gesture = entry.gesture;
    receipt.group = entry.group;
    receipt.coalesced = false;
    receipt.undoDepth = undoStack_.size();
    receipt.redoDepth = redoStack_.size();
    receipt.origin = std::move(by);
    // A group reaches what all of its commands reach, or anything.
    const auto first = entry.steps.front().command->reach();
    receipt.reach = std::all_of(entry.steps.begin(),
                                entry.steps.end(),
                                [first](const Step& step) { return step.command->reach() == first; })
                        ? first
                        : Reach::anything;
    return receipt;
}

} // namespace daw::domain
