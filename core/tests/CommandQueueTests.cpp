#include "TestSupport.h"
#include "daw/domain/command/CommandQueue.h"

#include <atomic>
#include <thread>

using namespace daw::domain;
using namespace daw::testing;

namespace
{

CommandQueue::Request addTrack(const TrackId& trackId, std::string name)
{
    CommandQueue::Request request{};
    request.origin.actor = Actor::copilot;
    request.steps.push_back(CommandQueue::Step{"track.add",
                                               Value::object({{"trackId", Value{trackId.toString()}},
                                                              {"name", Value{std::move(name)}},
                                                              {"volumeDb", Value{0.0}}})});
    return request;
}

} // namespace

TEST_CASE("a request submitted from another thread reaches the bus through the queue")
{
    Harness harness;
    CommandQueue queue;

    const auto trackId = TrackId::generate();

    // The socket thread: it never touches the bus, only the queue.
    std::future<CommandQueue::Answer> answer;
    std::thread caller{[&] { answer = queue.submit(addTrack(trackId, "Basse")); }};
    caller.join();

    REQUIRE(queue.pending() == 1);
    CHECK(harness.state.findTrack(trackId) == nullptr);

    // And the owning thread runs it.
    CHECK(queue.drain(harness.bus, harness.registry) == 1);

    const auto result = answer.get();
    REQUIRE_MESSAGE(result.ok(), result.message);
    CHECK(result.commandIds.size() == 1);
    CHECK(result.groupId.empty());
    REQUIRE(harness.state.findTrack(trackId) != nullptr);
    CHECK(harness.bus.undoDepth() == 1);
}

TEST_CASE("a labelled request is one history entry, whatever it holds")
{
    Harness harness;
    CommandQueue queue;

    const auto trackId = TrackId::generate();
    auto request = addTrack(trackId, "Basse");
    request.label = "ajoute une piste Basse et coupe-la";
    request.steps.push_back(CommandQueue::Step{
        "track.set_muted", Value::object({{"trackId", Value{trackId.toString()}}, {"muted", Value{true}}})});

    auto answer = queue.submit(std::move(request));
    CHECK(queue.drain(harness.bus, harness.registry) == 1);

    const auto result = answer.get();
    REQUIRE_MESSAGE(result.ok(), result.message);
    CHECK(result.commandIds.size() == 2);
    CHECK(!result.groupId.empty());
    CHECK(harness.bus.undoDepth() == 1);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findTrack(trackId) == nullptr);
}

TEST_CASE("an unknown type is refused before anything is applied")
{
    Harness harness;
    CommandQueue queue;

    const auto trackId = TrackId::generate();
    auto request = addTrack(trackId, "Basse");
    request.label = "ajoute une piste et fais n'importe quoi";
    request.steps.push_back(CommandQueue::Step{"track.explode", Value::object({})});

    auto answer = queue.submit(std::move(request));
    static_cast<void>(queue.drain(harness.bus, harness.registry));

    const auto result = answer.get();
    CHECK(!result.ok());
    CHECK(result.code == ErrorCode::unknownCommandType);
    CHECK(result.message.find("track.explode") != std::string::npos);

    // The first command of the request never ran.
    CHECK(harness.state.findTrack(trackId) == nullptr);
    CHECK(harness.bus.undoDepth() == 0);
}

TEST_CASE("an invalid payload from the queue is refused like any other")
{
    Harness harness;
    CommandQueue queue;

    CommandQueue::Request request{};
    request.origin.actor = Actor::copilot;
    request.steps.push_back(CommandQueue::Step{"track.set_volume",
                                               Value::object({{"trackId", Value{harness.trackId.toString()}},
                                                              {"volumeDb", Value{std::string{"fort"}}}})});

    auto answer = queue.submit(std::move(request));
    static_cast<void>(queue.drain(harness.bus, harness.registry));

    const auto result = answer.get();
    CHECK(!result.ok());
    CHECK(result.code == ErrorCode::typeMismatch);
    CHECK(harness.bus.undoDepth() == 0);
}

TEST_CASE("draining from the wrong thread refuses the request and changes nothing")
{
    Harness harness;
    CommandQueue queue;

    const auto trackId = TrackId::generate();
    auto answer = queue.submit(addTrack(trackId, "Basse"));

    // The bus belongs to this thread, so a drain from another one is refused
    // by the bus itself -- the queue adds no second rule of its own.
    std::thread stranger{[&] { static_cast<void>(queue.drain(harness.bus, harness.registry)); }};
    stranger.join();

    const auto result = answer.get();
    CHECK(!result.ok());
    CHECK(result.code == ErrorCode::wrongThread);
    CHECK(harness.state.findTrack(trackId) == nullptr);
}

TEST_CASE("closing the queue answers whoever was waiting instead of hanging them")
{
    Harness harness;
    CommandQueue queue;

    auto answer = queue.submit(addTrack(TrackId::generate(), "Basse"));
    queue.close();

    const auto result = answer.get();
    CHECK(!result.ok());
    CHECK(result.code == ErrorCode::conflict);

    // And what arrives afterwards is refused at once, not queued for a drain
    // that will never come.
    auto late = queue.submit(addTrack(TrackId::generate(), "Tard"));
    CHECK(!late.get().ok());
    CHECK(queue.drain(harness.bus, harness.registry) == 0);
}

TEST_CASE("the wake-up is called once per submission, from the submitting thread")
{
    CommandQueue queue;

    std::atomic<int> woken{0};
    queue.onSubmitted([&woken] { ++woken; });

    std::thread caller{[&]
                       {
                           static_cast<void>(queue.submit(addTrack(TrackId::generate(), "Une")));
                           static_cast<void>(queue.submit(addTrack(TrackId::generate(), "Deux")));
                       }};
    caller.join();

    CHECK(woken.load() == 2);
    CHECK(queue.pending() == 2);
}
