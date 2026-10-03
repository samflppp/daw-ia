#include "TestSupport.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/domain/serialization/Json.h"

#include <cmath>
#include <memory>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

PluginInstance effect(std::string_view identifier)
{
    PluginInstance instance{};
    instance.id = PluginId::generate();
    instance.ref = PluginRef{std::string{PluginRef::internalFormat}, std::string{identifier}, "effet"};
    return instance;
}

PluginInstance hosted()
{
    PluginInstance instance{};
    instance.id = PluginId::generate();
    instance.ref = PluginRef{std::string{PluginRef::vst3Format}, "uid", "Synthé"};
    return instance;
}

// The journal, replayed from its text on a fresh project holding the same
// track: the track comes back to the byte.
bool replayedTrackMatches(const Harness& recorded)
{
    ProjectState state;
    const auto registry = CommandRegistry::withBuiltinCommands();
    CommandBus bus{state, registry, BusLimits{}};
    Track track{};
    track.id = recorded.trackId;
    track.name = "Piste 1";
    REQUIRE(state.addTrack(track).ok());
    for (const auto& envelope : recorded.bus.journal())
    {
        const auto value = json::read(json::write(envelope));
        REQUIRE(value.ok());
        REQUIRE(bus.executeSerialized(value.value()).ok());
    }
    return json::write(state.findTrack(recorded.trackId)->toValue()) ==
           json::write(recorded.state.findTrack(recorded.trackId)->toValue());
}

} // namespace

TEST_CASE("an internal effect enters the chain by plugin.insert, and only a known one")
{
    Harness harness;
    CHECK(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, effect(internal::equaliser), 0))
              .ok());
    CHECK(
        harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, effect(internal::compressor), 1))
            .ok());
    CHECK_FALSE(
        harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, effect("daw.reverb"), 2)).ok());
    CHECK(harness.state.findTrack(harness.trackId)->plugins.size() == 2);
}

TEST_CASE("an internal parameter is in its own unit, bounded by the effect, and a hosted one stays 0..1")
{
    Harness harness;
    const auto eq = effect(internal::equaliser);
    const auto synth = hosted();
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, eq, 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, synth, 1)).ok());

    CHECK(harness.bus.execute(std::make_unique<SetPluginParameter>(eq.id, "mid1_freq", 1000.0)).ok());
    CHECK(harness.bus.execute(std::make_unique<SetPluginParameter>(eq.id, "mid1_gain", -6.0)).ok());
    CHECK_FALSE(harness.bus.execute(std::make_unique<SetPluginParameter>(eq.id, "mid1_gain", -21.0)).ok());
    CHECK_FALSE(harness.bus.execute(std::make_unique<SetPluginParameter>(eq.id, "hp_freq", 5000.0)).ok());
    CHECK_FALSE(harness.bus.execute(std::make_unique<SetPluginParameter>(eq.id, "drive", 0.5)).ok());

    CHECK(harness.bus.execute(std::make_unique<SetPluginParameter>(synth.id, "cutoff", 0.5)).ok());
    CHECK_FALSE(harness.bus.execute(std::make_unique<SetPluginParameter>(synth.id, "cutoff", 1000.0)).ok());

    const auto* held = harness.state.findPlugin(eq.id);
    REQUIRE(held != nullptr);
    CHECK(internalValue(*held, "mid1_gain") == -6.0);
    CHECK(internalValue(*held, "mid1_q") == 1.0); // the default, never written
}

TEST_CASE("an internal effect holds no blob")
{
    Harness harness;
    const auto eq = effect(internal::equaliser);
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, eq, 0)).ok());
    CHECK_FALSE(harness.bus
                    .execute(std::make_unique<CapturePluginState>(
                        eq.id, StateBlobRef{std::string(BlobRef::digestLength, 'a'), 12}))
                    .ok());

    auto withBlob = effect(internal::compressor);
    withBlob.state = StateBlobRef{std::string(BlobRef::digestLength, 'b'), 4};
    CHECK_FALSE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, withBlob, 1)).ok());
}

TEST_CASE("a project with internal effects replays to the byte")
{
    Harness harness;
    const auto eq = effect(internal::equaliser);
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, eq, 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(eq.id, "hp_freq", 80.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(eq.id, "low_gain", -3.5)).ok());

    const auto written = json::write(harness.state.toValue());
    auto read = ProjectState::fromValue(harness.state.toValue());
    REQUIRE(read.ok());
    CHECK(json::write(read.value().toValue()) == written);
    CHECK(replayedTrackMatches(harness));
}

TEST_CASE("the equaliser's curve: a bell of -6 dB at 1 kHz is -6 dB there and nothing far away")
{
    auto eq = effect(internal::equaliser);
    eq.params = {{"mid1_freq", 1000.0}, {"mid1_gain", -6.0}, {"mid1_q", 1.0}};
    CHECK(equaliserGainDb(eq, 1000.0) == doctest::Approx(-6.0).epsilon(0.001));
    CHECK(std::abs(equaliserGainDb(eq, 40.0)) < 0.1);
    CHECK(std::abs(equaliserGainDb(eq, 16000.0)) < 0.2);

    // A second-order high-pass is 3 dB down at its frequency, 12 dB an octave
    // further down... give or take the bilinear warping at 48 kHz.
    auto cut = effect(internal::equaliser);
    cut.params = {{"hp_freq", 100.0}};
    CHECK(equaliserGainDb(cut, 100.0) == doctest::Approx(-3.01).epsilon(0.01));
    CHECK(equaliserGainDb(cut, 25.0) < -20.0);
    CHECK(std::abs(equaliserGainDb(cut, 1000.0)) < 0.05);

    // Bypassed, it does nothing.
    eq.bypassed = true;
    CHECK(equaliserGainDb(eq, 1000.0) == 0.0);
}
