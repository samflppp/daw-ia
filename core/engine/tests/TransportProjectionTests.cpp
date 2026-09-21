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

TEST_CASE("Stopping keeps the playhead where it was")
{
    EngineHarness harness;

    REQUIRE(harness.state.setTempoPointBpm(ProjectState::originTempoPointId(), 120.0).ok());
    harness.projector.reconcile();

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetPosition>(4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportStop>()).ok());

    CHECK_FALSE(harness.host.edit().getTransport().isPlaying());
    CHECK(harness.host.edit().getTransport().getPosition().inSeconds() == doctest::Approx(2.0));
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
