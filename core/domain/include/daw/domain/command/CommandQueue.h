#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/CommandBus.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace daw::domain
{

// The way a command reaches the bus from another thread.
//
// The copilot lives in a separate process and talks over a socket, so its
// commands arrive on whatever thread reads that socket. The bus belongs to the
// thread that built it — the message thread, where Tracktion expects its Edit
// to be touched — and refuses every other one with ErrorCode::wrongThread.
//
// The answer is a queue, not a handover. rebindToCurrentThread() exists for
// the deliberate passing of the bus from one thread to another, once; using it
// per command would mean two threads taking turns owning the bus, which is a
// race written in a polite way. Here the socket thread only ever touches this
// queue, and the owning thread is the only one that ever touches the bus.
//
// What crosses the boundary is the intention, in the same form the journal and
// JSON-RPC use: a command type and a payload. Nothing else can: a Command is
// not copyable and the bus is not reachable from here.
class CommandQueue
{
public:
    // One command as it arrives from outside: the wire name and the payload.
    struct Step
    {
        std::string type;
        Value payload;
    };

    // One request. Several steps make one history entry when a label is given,
    // because that is what a copilot request is: several commands, one thing
    // asked. Without a label each step is its own entry, exactly as if it had
    // been executed alone.
    struct Request
    {
        std::string label;
        Provenance origin{};
        std::vector<Step> steps;
    };

    // What the caller gets back, once the owning thread has run the request.
    // The identifiers are the ones the bus minted, so the answer can name what
    // was created without the caller guessing.
    struct Answer
    {
        ErrorCode code{ErrorCode::none};
        std::string message;
        std::string groupId;
        std::vector<std::string> commandIds;

        [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::none; }
    };

    CommandQueue() = default;
    ~CommandQueue();

    CommandQueue(const CommandQueue&) = delete;
    CommandQueue& operator=(const CommandQueue&) = delete;
    CommandQueue(CommandQueue&&) = delete;
    CommandQueue& operator=(CommandQueue&&) = delete;

    // Callable from any thread. The future is fulfilled by whoever calls
    // drain(), or by close() if the application stops first: a caller waiting
    // on a queue nobody will drain again is a hung socket thread.
    [[nodiscard]] std::future<Answer> submit(Request request);

    // Called from any thread, right after submit(), so the owning thread knows
    // there is something to drain without polling. The application wires the
    // framework's own wake-up here — an AsyncUpdater, a timer, whatever it has
    // — and the domain never learns which.
    void onSubmitted(std::function<void()> wakeUp);

    // Runs everything that is waiting, in the order it arrived, and answers
    // each caller. Returns how many requests it ran.
    //
    // It must be called from the thread that owns the bus, and it is the bus
    // itself that enforces it: every request is refused with wrongThread and
    // the project is untouched. Saying it twice would be one check too many,
    // and the one that mattered would be the one that drifted.
    std::size_t drain(CommandBus& bus, const CommandRegistry& registry);

    // Answers every waiting caller with an error and refuses the ones that
    // come later. Called when the application is shutting down.
    void close();

    [[nodiscard]] bool isClosed() const;
    [[nodiscard]] std::size_t pending() const;

private:
    struct Pending
    {
        Request request;
        std::promise<Answer> answer;
    };

    [[nodiscard]] static Answer run(Request& request, CommandBus& bus, const CommandRegistry& registry);

    mutable std::mutex mutex_;
    std::deque<Pending> pending_;
    std::function<void()> wakeUp_;
    bool closed_{false};
};

} // namespace daw::domain
