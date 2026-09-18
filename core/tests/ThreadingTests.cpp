#include "TestSupport.h"

#include <thread>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

// Runs the body on another thread and waits for it. Anything the body records
// is safe to read afterwards: the join is the synchronisation.
template <typename Body>
void onAnotherThread(Body&& body)
{
    std::thread worker{std::forward<Body>(body)};
    worker.join();
}

} // namespace

TEST_CASE("The bus belongs to the thread that constructed it")
{
    Harness harness;
    CHECK(harness.bus.owningThread() == std::this_thread::get_id());
}

TEST_CASE("A command from another thread is refused, and changes nothing")
{
    Harness harness;
    const auto before = harness.state.toValue();

    ErrorCode code{ErrorCode::none};
    onAnotherThread([&harness, &code] { code = harness.bus.execute(harness.setVolume(-6.0)).code(); });

    CHECK(code == ErrorCode::wrongThread);
    CHECK(harness.state.toValue() == before);
    CHECK(harness.bus.undoDepth() == 0);
}

TEST_CASE("Every mutating entry point is guarded, not just execute")
{
    Harness harness;
    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());
    REQUIRE(harness.bus.undo().ok());

    const auto gesture = harness.bus.beginGesture("fader");

    ErrorCode undoCode{ErrorCode::none};
    ErrorCode redoCode{ErrorCode::none};
    ErrorCode serializedCode{ErrorCode::none};
    ErrorCode gestureCode{ErrorCode::none};

    onAnotherThread(
        [&]
        {
            undoCode = harness.bus.undo().code();
            redoCode = harness.bus.redo().code();
            serializedCode = harness.bus.executeSerialized(Value::object({})).code();
            gestureCode = harness.bus.endGesture(gesture).code();
        });

    CHECK(undoCode == ErrorCode::wrongThread);
    CHECK(redoCode == ErrorCode::wrongThread);
    CHECK(serializedCode == ErrorCode::wrongThread);
    CHECK(gestureCode == ErrorCode::wrongThread);

    // The thread check comes before anything else: a malformed envelope from
    // the wrong thread reports the thread, not the envelope.
    CHECK(harness.bus.redoDepth() == 1);
}

TEST_CASE("A deliberate handover moves the bus to another thread")
{
    Harness harness;

    ErrorCode rebindCode{ErrorCode::none};
    ErrorCode executeCode{ErrorCode::none};

    onAnotherThread(
        [&]
        {
            // Refused: the handover has to be decided by the owner, not taken
            // by whoever asks first.
            rebindCode = harness.bus.rebindToCurrentThread().code();
            executeCode = harness.bus.execute(harness.setVolume(-3.0)).code();
        });

    CHECK(rebindCode == ErrorCode::wrongThread);
    CHECK(executeCode == ErrorCode::wrongThread);

    // Handing it over from the owning thread is not possible either: the new
    // owner is whoever calls, so the owner calls it on the thread it wants.
    // Here the owner gives it to itself, which is a no-op but a legal one.
    REQUIRE(harness.bus.rebindToCurrentThread().ok());
    CHECK(harness.bus.owningThread() == std::this_thread::get_id());
    CHECK(harness.bus.execute(harness.setVolume(-3.0)).ok());
}

TEST_CASE("An observer cannot hand the bus to itself mid-notification")
{
    Harness harness;

    class RebindingObserver final : public BusObserver
    {
    public:
        explicit RebindingObserver(CommandBus& bus)
            : bus_{bus}
        {
        }

        void onExecuted(const Receipt& receipt) override
        {
            static_cast<void>(receipt);
            code = bus_.rebindToCurrentThread().code();
        }

        ErrorCode code{ErrorCode::none};

    private:
        CommandBus& bus_;
    };

    RebindingObserver observer{harness.bus};
    harness.bus.addObserver(observer);

    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());
    CHECK(observer.code == ErrorCode::reentrantCall);
}
