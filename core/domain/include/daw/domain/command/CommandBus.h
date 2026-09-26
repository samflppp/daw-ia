#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace daw::domain
{

struct ExecuteOptions
{
    // Set it to the identifier returned by beginGesture() to allow the command
    // to merge with the previous one of the same gesture. Left empty, the
    // command always creates its own history entry.
    std::optional<GestureId> gesture;

    // Who is asking. The default is the user, so every call site written
    // before provenance existed keeps telling the truth.
    Provenance origin{};
};

// What a group needs to exist: a label for the history, and an author.
//
// The bus mints the group identifier, exactly as it mints the identifier of a
// command. That is not the rule about identifiers being broken: the rule
// forbids a *command* from engendering an identifier the project state depends
// on, because a replayed payload would then build a different project. A group
// names a history entry, never a thing in the project, and a replay reads it
// from the journal instead of inventing it.
struct GroupOptions
{
    // Shown in the history panel. For the copilot, the request the user typed.
    std::string label;

    Provenance origin{};
};

struct BusLimits
{
    // Beyond this depth the oldest entry is dropped and observers are told.
    std::size_t maxUndoDepth{512};
};

// A command, an undo or a redo the bus turned down, with the bus as it was at
// that moment. It exists for one reason: an action that "had no effect" during
// playback, twice, never reproduced. Whatever refused it, and in which state,
// is now written down instead of discarded by a static_cast<void>.
struct Refusal
{
    std::string operation;   // "execute", "group", "undo" or "redo"
    std::string commandType; // empty for an undo or a redo
    Error error;
    std::thread::id caller;
    std::thread::id owner;
    bool mutating{false}; // true when refused from inside another command
    std::string openGesture;
    std::size_t undoDepth{0};
    std::size_t redoDepth{0};
};

// Executes commands, keeps the undo and redo stacks, notifies observers, and
// produces the journal the versioning layer will persist.
//
// The bus owns no state of its own about the project: it holds a reference to
// a ProjectState and only touches it through commands.
//
// Threading: the bus belongs to the thread that constructed it, and to that
// thread only. The engine projects onto a Tracktion Edit from inside the
// notifications, and Tracktion expects that on the message thread, so a
// command arriving from anywhere else would mutate an Edit under Tracktion's
// feet. The rule is checked, not merely documented: every mutating entry
// point refuses a call from another thread with ErrorCode::wrongThread, in
// release as well as in debug, because the day a Python service pushes
// commands in from a socket the mistake must be an error and not a crash
// three layers down.
//
// rebindToCurrentThread() exists for the deliberate handover: loading a
// project on a worker thread, then giving the bus to the message thread.
//
// beginGesture, clearHistory, addObserver and removeObserver are subject to
// the same rule but return no Result, so they assert in debug instead. That
// costs them nothing in release and loses no safety: none of them mutates the
// project, and the command that would has to go through execute().
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

    // Runs several commands as one history entry: one line in the panel, one
    // Ctrl+Z, whatever their types. This is what a copilot request needs —
    // "add a Bass track and put Vital on it" is two commands and one thing
    // asked — and what a user macro will need later.
    //
    // All or nothing. Every command is applied before a single observer is
    // told; if one fails, the ones already applied are reverted in reverse
    // order, no notification is sent, no journal row is written, and the error
    // of the failing command is returned. The project is never left half
    // modified.
    //
    // One receipt per command is returned and notified, all carrying the same
    // group: the journal keeps a row per command, append-only, each replayable
    // on its own, and the history folds them into one entry.
    //
    // Transient commands may sit in a group — "set the tempo to 140 and loop
    // the clip" is one request — and they enter no history entry, exactly as
    // they do alone. A group of nothing but transient commands leaves no entry
    // at all. One consequence is written down rather than hidden: a transient
    // command that already ran when a later one fails stays applied, because
    // the transport is not project state and reverting it would invent a
    // position the user never asked for.
    Result<std::vector<Receipt>> executeGroup(std::vector<std::unique_ptr<Command>> commands,
                                              GroupOptions options);

    // Replay path: rebuilds the command from the registry and keeps the
    // identifier and the date of the envelope, so a replayed history is the
    // same history. Never coalesces: an envelope already is the merged form.
    Result<Receipt> executeSerialized(const Value& envelope);

    // Replay path for a group: the envelopes rebuild one history entry, with
    // the group they carry. Every envelope must name the same group, because a
    // replay that silently split a group would give back a project with the
    // right state and the wrong history.
    Result<std::vector<Receipt>> executeSerializedGroup(const std::vector<Value>& envelopes);

    // --- history -----------------------------------------------------------
    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    // by names who asks for the move, not who wrote the command: a copilot
    // undoing a user's edit is a fact the journal keeps.
    Result<Receipt> undo(Provenance by = {});
    Result<Receipt> redo(Provenance by = {});
    [[nodiscard]] std::size_t undoDepth() const noexcept;
    [[nodiscard]] std::size_t redoDepth() const noexcept;
    void clearHistory() noexcept;

    // --- gestures ----------------------------------------------------------
    // One gesture at a time. Opening a second one closes the first.
    [[nodiscard]] GestureId beginGesture(std::string_view label);
    Result<void> endGesture(GestureId gesture);
    [[nodiscard]] std::optional<GestureId> openGesture() const noexcept;
    [[nodiscard]] std::string_view openGestureLabel() const noexcept;

    // --- threading ---------------------------------------------------------
    [[nodiscard]] std::thread::id owningThread() const noexcept;

    // Transfers ownership to the calling thread. Refused while a command is
    // being applied, and refused from any thread but the current owner: a
    // handover is a decision, never a race.
    Result<void> rebindToCurrentThread();

    // --- observers ---------------------------------------------------------
    ObserverToken addObserver(BusObserver& observer);
    void removeObserver(ObserverToken token) noexcept;

    // --- diagnostics -------------------------------------------------------
    // Told of every refusal of execute, executeGroup, undo and redo. It is
    // called on the thread that was refused, which is not the owner's when
    // the refusal is wrongThread, and possibly from inside another command:
    // it must write the refusal down and never call the bus back.
    void setRefusalListener(std::function<void(const Refusal&)> listener);

    // --- journal -----------------------------------------------------------
    // The envelopes that build the current state, oldest first, after merging.
    // Replaying them on an empty project reproduces the state exactly.
    [[nodiscard]] std::vector<Value> journal() const;

private:
    // One command that ran, with what it overwrote. An entry holds one of
    // these, or several when a group made them one action.
    struct Step
    {
        CommandId id{};
        Timestamp at{};
        std::unique_ptr<Command> command;
        Value undoRecord;
    };

    // One history entry: what a single Ctrl+Z gives back. Never empty — an
    // entry with no step would be a line in the panel that undoes nothing.
    struct Entry
    {
        std::optional<GestureId> gesture;
        std::optional<GroupRef> group;
        Provenance origin{};
        std::vector<Step> steps;

        [[nodiscard]] const Step& first() const noexcept { return steps.front(); }
    };

    struct Registration
    {
        ObserverToken token;
        BusObserver* observer{nullptr};
    };

    // The public entry points, before a refusal is reported.
    Result<Receipt> executeChecked(std::unique_ptr<Command> command, ExecuteOptions options);
    Result<std::vector<Receipt>> executeGroupChecked(std::vector<std::unique_ptr<Command>> commands,
                                                     GroupOptions options);
    Result<Receipt> undoChecked(Provenance by);
    Result<Receipt> redoChecked(Provenance by);
    void reportRefusal(std::string_view operation, std::string commandType, const Error& error) const;

    Result<Receipt> executeEntry(std::unique_ptr<Command> command,
                                 CommandId id,
                                 Timestamp at,
                                 std::optional<GestureId> gesture,
                                 Provenance origin,
                                 bool allowCoalescing);

    // The shared body of executeGroup and executeSerializedGroup: the
    // identifiers and dates are given, so a replayed group is the same group.
    struct GroupCommand
    {
        CommandId id{};
        Timestamp at{};
        std::unique_ptr<Command> command;
    };

    Result<std::vector<Receipt>>
    executeGroupEntry(std::vector<GroupCommand> commands, GroupRef group, Provenance origin);

    // Drops the oldest entries when the stack grew past the limit, and says
    // how many went. Observers are told separately, once the entry that caused
    // it is in place.
    std::size_t trimToLimit();

    [[nodiscard]] Result<void> checkThread() const;
    [[nodiscard]] Receipt receiptFor(const Entry& entry, const Step& step, bool coalesced) const;
    [[nodiscard]] Receipt moveReceiptFor(const Entry& entry, Provenance by) const;
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
    std::thread::id owningThread_{std::this_thread::get_id()};
    std::function<void(const Refusal&)> refusalListener_;
};

} // namespace daw::domain
