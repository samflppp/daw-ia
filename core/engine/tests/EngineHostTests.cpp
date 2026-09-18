#include "EngineTestSupport.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

TEST_CASE("The engine has its outputs as soon as the host is constructed")
{
    EngineHarness harness;
    auto& deviceManager = harness.host.engine().getDeviceManager();

    // Tracktion builds its wave device list from an async update, so without an
    // explicit flush the list is still empty when the constructor returns. The
    // Edit then plays into nothing: the transport says it runs, and not a
    // single sample leaves the machine.
    CHECK(deviceManager.getNumWaveOutDevices() > 0);
    CHECK(deviceManager.getDefaultWaveOutDevice() != nullptr);
}

TEST_CASE("Playing produces a playback context bound to an output")
{
    EngineHarness harness;

    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 60)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportPlay>()).ok());

    CHECK(harness.host.edit().getTransport().isPlayContextActive());
    CHECK(harness.host.engine().getDeviceManager().getDefaultWaveOutDevice() != nullptr);
}
