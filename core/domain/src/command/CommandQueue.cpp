#include "daw/domain/command/CommandQueue.h"

#include <utility>

namespace daw::domain
{
namespace
{

CommandQueue::Answer answerFrom(const Error& error)
{
    CommandQueue::Answer answer{};
    answer.code = error.code;
    answer.message = error.message;
    return answer;
}

} // namespace

CommandQueue::~CommandQueue()
{
    close();
}

std::future<CommandQueue::Answer> CommandQueue::submit(Request request)
{
    std::promise<Answer> promise;
    auto future = promise.get_future();

    std::function<void()> wakeUp;
    {
        const std::lock_guard<std::mutex> lock{mutex_};

        if (closed_)
        {
            promise.set_value(answerFrom(fail(ErrorCode::conflict, "the command queue is closed")));
            return future;
        }

        Pending entry{};
        entry.request = std::move(request);
        entry.answer = std::move(promise);
        pending_.push_back(std::move(entry));
        wakeUp = wakeUp_;
    }

    // Outside the lock: the wake-up crosses into the framework, and holding a
    // mutex while another thread's message loop runs is how two threads end up
    // waiting for each other.
    if (wakeUp)
        wakeUp();

    return future;
}

void CommandQueue::onSubmitted(std::function<void()> wakeUp)
{
    const std::lock_guard<std::mutex> lock{mutex_};
    wakeUp_ = std::move(wakeUp);
}

std::size_t CommandQueue::drain(CommandBus& bus, const CommandRegistry& registry)
{
    std::deque<Pending> batch;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        batch.swap(pending_);
    }

    // The queue is empty again before the first command runs, so a command
    // that submits another one during a notification is simply drained next
    // time round instead of growing the batch under our feet.
    for (auto& entry : batch)
        entry.answer.set_value(run(entry.request, bus, registry));

    return batch.size();
}

CommandQueue::Answer CommandQueue::run(Request& request, CommandBus& bus, const CommandRegistry& registry)
{
    if (request.steps.empty())
        return answerFrom(fail(ErrorCode::invalidArgument, "the request holds no command"));

    // Every command is built before any of them runs: an unknown type or a
    // malformed payload is a refusal, never a project half changed.
    std::vector<std::unique_ptr<Command>> commands;
    commands.reserve(request.steps.size());
    for (auto& step : request.steps)
    {
        auto command = registry.create(step.type, step.payload);
        if (!command)
            return answerFrom(fail(command.error().code, step.type + ": " + command.error().message));

        commands.push_back(std::move(command).value());
    }

    Answer answer{};

    // A request with no label is not a group: one command executed alone has
    // to leave the same history entry whether it came from a socket or from a
    // button.
    if (request.label.empty() && commands.size() == 1)
    {
        ExecuteOptions options{};
        options.origin = request.origin;

        auto receipt = bus.execute(std::move(commands.front()), options);
        if (!receipt)
            return answerFrom(receipt.error());

        answer.commandIds.push_back(receipt.value().id.toString());
        return answer;
    }

    // A group without a label would put a line in the history that says
    // nothing. Whoever asks for several commands at once says what they asked
    // for, and the copilot has the sentence the user typed.
    GroupOptions options{};
    options.label = std::move(request.label);
    options.origin = request.origin;

    auto receipts = bus.executeGroup(std::move(commands), std::move(options));
    if (!receipts)
        return answerFrom(receipts.error());

    for (const auto& receipt : receipts.value())
    {
        answer.commandIds.push_back(receipt.id.toString());
        if (receipt.group.has_value())
            answer.groupId = receipt.group->id.toString();
    }

    return answer;
}

void CommandQueue::close()
{
    std::deque<Pending> abandoned;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        if (closed_)
            return;

        closed_ = true;
        abandoned.swap(pending_);
        wakeUp_ = nullptr;
    }

    for (auto& entry : abandoned)
        entry.answer.set_value(answerFrom(fail(ErrorCode::conflict, "the command queue is closed")));
}

bool CommandQueue::isClosed() const
{
    const std::lock_guard<std::mutex> lock{mutex_};
    return closed_;
}

std::size_t CommandQueue::pending() const
{
    const std::lock_guard<std::mutex> lock{mutex_};
    return pending_.size();
}

} // namespace daw::domain
