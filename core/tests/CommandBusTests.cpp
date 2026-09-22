#include "TestSupport.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
using daw::testing::RecordingObserver;

TEST_CASE("A successful command creates one entry and clears the redo stack")
{
    Harness harness;
    RecordingObserver observer;
    harness.bus.addObserver(observer);

    const auto receipt = harness.bus.execute(harness.createClip(ClipId::generate()));
    REQUIRE(receipt.ok());
    CHECK(receipt.value().type == "clip.create_midi");
    CHECK_FALSE(receipt.value().coalesced);
    CHECK(harness.bus.undoDepth() == 1);
    CHECK(harness.bus.redoDepth() == 0);
    REQUIRE(observer.executed.size() == 1);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.bus.redoDepth() == 1);

    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());
    CHECK(harness.bus.redoDepth() == 0);
}

TEST_CASE("A failed command leaves no trace")
{
    Harness harness;
    RecordingObserver observer;
    harness.bus.addObserver(observer);

    const auto before = harness.state.toValue();

    SUBCASE("note added to a clip that does not exist")
    {
        auto command = Harness::addNote(ClipId::generate(), NoteId::generate());
        CHECK(harness.bus.execute(std::move(command)).code() == ErrorCode::notFound);
    }

    SUBCASE("volume out of range")
    {
        CHECK(harness.bus.execute(harness.setVolume(99.0)).code() == ErrorCode::invalidArgument);
    }

    SUBCASE("clip on a track that does not exist")
    {
        auto command = std::make_unique<CreateMidiClip>(TrackId::generate(), ClipId::generate(), 0.0, 4.0);
        CHECK(harness.bus.execute(std::move(command)).code() == ErrorCode::notFound);
    }

    CHECK(harness.state.toValue() == before);
    CHECK(harness.bus.undoDepth() == 0);
    CHECK(harness.bus.canUndo() == false);
    CHECK(observer.executed.empty());
}

TEST_CASE("An empty history refuses undo and redo")
{
    Harness harness;

    CHECK(harness.bus.undo().code() == ErrorCode::nothingToUndo);
    CHECK(harness.bus.redo().code() == ErrorCode::nothingToRedo);
}

TEST_CASE("A null command is refused")
{
    Harness harness;
    CHECK(harness.bus.execute(nullptr).code() == ErrorCode::invalidArgument);
}

TEST_CASE("Observers are notified after the state has changed")
{
    Harness harness;

    class StateReadingObserver final : public BusObserver
    {
    public:
        StateReadingObserver(const ProjectState& state, ClipId clipId)
            : state_{state}
            , clipId_{clipId}
        {
        }

        void onExecuted(const Receipt& receipt) override
        {
            static_cast<void>(receipt);
            sawClip = state_.findClip(clipId_) != nullptr;
        }

        bool sawClip{false};

    private:
        const ProjectState& state_;
        ClipId clipId_;
    };

    const auto clipId = ClipId::generate();
    StateReadingObserver observer{harness.state, clipId};
    harness.bus.addObserver(observer);

    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    CHECK(observer.sawClip);
}

TEST_CASE("An observer cannot re-enter the bus")
{
    Harness harness;

    class ReentrantObserver final : public BusObserver
    {
    public:
        ReentrantObserver(CommandBus& bus, Harness& harness)
            : bus_{bus}
            , harness_{harness}
        {
        }

        void onExecuted(const Receipt& receipt) override
        {
            static_cast<void>(receipt);
            executeCode = bus_.execute(harness_.createClip(ClipId::generate())).code();
            undoCode = bus_.undo().code();
            redoCode = bus_.redo().code();
        }

        ErrorCode executeCode{ErrorCode::none};
        ErrorCode undoCode{ErrorCode::none};
        ErrorCode redoCode{ErrorCode::none};

    private:
        CommandBus& bus_;
        Harness& harness_;
    };

    ReentrantObserver observer{harness.bus, harness};
    harness.bus.addObserver(observer);

    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());

    CHECK(observer.executeCode == ErrorCode::reentrantCall);
    CHECK(observer.undoCode == ErrorCode::reentrantCall);
    CHECK(observer.redoCode == ErrorCode::reentrantCall);
    CHECK(harness.bus.undoDepth() == 1);
}

TEST_CASE("A removed observer is no longer notified")
{
    Harness harness;
    RecordingObserver first;
    RecordingObserver second;

    const auto firstToken = harness.bus.addObserver(first);
    harness.bus.addObserver(second);

    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());
    harness.bus.removeObserver(firstToken);
    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());

    CHECK(first.executed.size() == 1);
    CHECK(second.executed.size() == 2);
}

TEST_CASE("The history has a bound, and says when it drops an entry")
{
    Harness harness{BusLimits{3}};
    RecordingObserver observer;
    harness.bus.addObserver(observer);

    for (int index = 0; index < 5; ++index)
        REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());

    CHECK(harness.bus.undoDepth() == 3);
    CHECK(observer.truncated == 2);

    // The five patterns are all there: truncating the history forgets how to
    // undo, it never touches the project.
    CHECK(harness.state.patterns().size() == 5);
    CHECK(harness.state.arrangement().size() == 5);
}

TEST_CASE("clearHistory forgets the past but keeps the project")
{
    Harness harness;
    REQUIRE(harness.bus.execute(harness.createClip(ClipId::generate())).ok());
    REQUIRE(harness.bus.undo().ok());

    const auto before = harness.state.toValue();
    harness.bus.clearHistory();

    CHECK(harness.bus.undoDepth() == 0);
    CHECK(harness.bus.redoDepth() == 0);
    CHECK(harness.state.toValue() == before);
}
