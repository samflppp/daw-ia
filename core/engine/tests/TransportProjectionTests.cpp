#include "EngineTestSupport.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

TEST_CASE("transport.play starts the Tracktion transport, transport.stop stops it")
{
    EngineHarness harness;

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    CHECK(harness.host.edit().getTransport().isPlaying());

    REQUIRE(harness.bus.execute(std::make_unique<TransportStop>()).ok());
    CHECK_FALSE(harness.host.edit().getTransport().isPlaying());
}

TEST_CASE("transport.set_position moves the playhead, in beats")
{
    EngineHarness harness;

    REQUIRE(harness.state.setTempoPointBpm(ProjectState::originTempoPointId(), 120.0).ok());
    harness.projector.reconcile();

    // At 120 BPM a beat lasts half a second, so beat 8 is second 4.
    REQUIRE(harness.bus.execute(std::make_unique<TransportSetPosition>(8.0)).ok());

    CHECK(harness.host.edit().getTransport().getPosition().inSeconds() == doctest::Approx(4.0));
}

TEST_CASE("Stopping returns the playhead to the start")
{
    EngineHarness harness;

    REQUIRE(harness.state.setTempoPointBpm(ProjectState::originTempoPointId(), 120.0).ok());
    harness.projector.reconcile();

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetPosition>(4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportStop>()).ok());

    CHECK_FALSE(harness.host.edit().getTransport().isPlaying());
    CHECK(harness.host.edit().getTransport().getPosition().inSeconds() == doctest::Approx(0.0));
    CHECK(harness.state.transport().positionBeats == doctest::Approx(0.0));
}

TEST_CASE("Play reaches the engine even when the domain already said playing")
{
    EngineHarness harness;

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    REQUIRE(harness.host.edit().getTransport().isPlaying());

    // Exactly what happens in use: playback runs off the end of the material
    // and Tracktion stops by itself. Nothing tells the domain, so it still
    // says "playing" -- and the next press of the button used to change
    // nothing, therefore reach nothing.
    harness.host.edit().getTransport().stop(false, false);
    REQUIRE_FALSE(harness.host.edit().getTransport().isPlaying());
    REQUIRE(harness.state.transport().playing);

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    CHECK(harness.host.edit().getTransport().isPlaying());
}

TEST_CASE("Rewinding reaches the engine even when the domain already held zero")
{
    EngineHarness harness;

    REQUIRE(harness.state.setTempoPointBpm(ProjectState::originTempoPointId(), 120.0).ok());
    harness.projector.reconcile();

    // The domain has never been asked to move the playhead, so it holds 0.
    REQUIRE(harness.state.transport().positionBeats == doctest::Approx(0.0));

    // The engine has moved on its own, the way playback does.
    harness.host.edit().getTransport().setPosition(tracktion::TimePosition::fromSeconds(3.0));
    REQUIRE(harness.host.edit().getTransport().getPosition().inSeconds() == doctest::Approx(3.0));

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetPosition>(0.0)).ok());
    CHECK(harness.host.edit().getTransport().getPosition().inSeconds() == doctest::Approx(0.0));
}

TEST_CASE("A transient command projects without touching the history")
{
    EngineHarness harness;

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());

    CHECK(harness.host.edit().getTransport().isPlaying());
    CHECK(harness.bus.undoDepth() == 0);
    CHECK(harness.bus.journal().empty());
}

TEST_CASE("A project change while playing does not restart playback")
{
    EngineHarness harness;

    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    REQUIRE(harness.host.edit().getTransport().isPlaying());

    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 60)).ok());

    // Each of those reconciled the whole project. The transport was asked for
    // nothing, because nothing about it changed.
    CHECK(harness.host.edit().getTransport().isPlaying());
}

TEST_CASE("transport.set_loop reaches the engine, in beats")
{
    EngineHarness harness;

    REQUIRE(harness.state.setTempoPointBpm(ProjectState::originTempoPointId(), 120.0).ok());
    harness.projector.reconcile();

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetLoop>(true, 4.0, 8.0)).ok());

    // At 120 BPM a beat lasts half a second: beats 4 to 8 are seconds 2 to 4.
    CHECK(harness.host.edit().getTransport().looping.get());
    CHECK(harness.host.edit().getTransport().getLoopRange().getStart().inSeconds() == doctest::Approx(2.0));
    CHECK(harness.host.edit().getTransport().getLoopRange().getEnd().inSeconds() == doctest::Approx(4.0));

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetLoop>(false, 0.0, 0.0)).ok());
    CHECK_FALSE(harness.host.edit().getTransport().looping.get());
}

TEST_CASE("A loop that ends where it starts is refused, and the engine is untouched")
{
    EngineHarness harness;

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetLoop>(true, 4.0, 8.0)).ok());

    CHECK(harness.bus.execute(std::make_unique<TransportSetLoop>(true, 4.0, 4.0)).error().code ==
          ErrorCode::invalidArgument);

    CHECK(harness.host.edit().getTransport().getLoopRange().getLength().inSeconds() > 0.0);
}
