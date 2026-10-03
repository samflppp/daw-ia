#include "EngineTestSupport.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/MixRender.h"
#include "daw/engine/MixTap.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <thread>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::engine::MixRender;

namespace
{

constexpr double toneRate = 44100.0;
constexpr double toneSeconds = 6.0;

juce::MemoryBlock tone(double frequency, double levelDb)
{
    juce::AudioBuffer<float> buffer{1, static_cast<int>(toneSeconds * toneRate)};
    const auto amplitude = std::pow(10.0, levelDb / 20.0);
    for (int index = 0; index < buffer.getNumSamples(); ++index)
        buffer.setSample(
            0,
            index,
            static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency * index / toneRate)));
    juce::MemoryBlock bytes;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(
            stream,
            juce::AudioFormatWriterOptions{}.withSampleRate(toneRate).withNumChannels(1).withBitsPerSample(
                24));
        REQUIRE(writer != nullptr);
        REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
    }
    return bytes;
}

struct MixHarness
{
    MixHarness()
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry}
        , host{"daw_engine_tests"}
        , folder{juce::File::createTempFile("mixrender")}
        , store{folder.getChildFile("blobs")}
        , projector{host.edit(), state, nullptr, &store}
    {
        bus.addObserver(projector);
        lows = addTone("Basse", 60.0, -12.0);
        highs = addTone("Lead", 1000.0, -12.0);
    }

    ~MixHarness() { static_cast<void>(folder.deleteRecursively()); }

    TrackId addTone(const std::string& name, double frequency, double levelDb)
    {
        const auto id = TrackId::generate();
        REQUIRE(bus.execute(std::make_unique<AddTrack>(id, name, 0.0)).ok());
        const auto bytes = tone(frequency, levelDb);
        auto blob = store.put(bytes.getData(), bytes.getSize());
        REQUIRE(blob.ok());
        SampleRef sample;
        sample.blob = blob.value();
        sample.name = name + ".wav";
        sample.format = "wav";
        sample.seconds = toneSeconds;
        REQUIRE(bus.execute(std::make_unique<PlaceAudio>(AudioClipId::generate(), id, sample, 0.0)).ok());
        return id;
    }

    std::unique_ptr<MixRender::Measured> measure(const ProjectState* proposed = nullptr)
    {
        auto render = MixRender::prepare(host.edit(), state, proposed, nullptr, &store);
        REQUIRE(render != nullptr);
        std::unique_ptr<MixRender::Measured> measured;
        std::atomic<bool> cancelled{false};

        // Off the message thread, the way the application runs it; the
        // message loop keeps turning meanwhile, as it does in the application.
        std::atomic<bool> done{false};
        std::thread worker{[&]
                           {
                               measured = render->run(cancelled, {});
                               done = true;
                           }};
        const auto until = juce::Time::getMillisecondCounterHiRes() + 120000.0;
        while (!done && juce::Time::getMillisecondCounterHiRes() < until)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        if (!done)
            cancelled = true;
        worker.join();
        REQUIRE(measured != nullptr);
        return measured;
    }

    int tapsInLiveEdit()
    {
        int count = 0;
        for (auto* track : tracktion::getAudioTracks(host.edit()))
            count += track->pluginList.getPluginsOfType<daw::engine::MixTap>().size();
        return count + host.edit().getMasterPluginList().getPluginsOfType<daw::engine::MixTap>().size();
    }

    ProjectState state;
    CommandRegistry registry;
    CommandBus bus;
    daw::engine::EngineHost host;
    juce::File folder;
    daw::engine::ContentStore store;
    daw::engine::ProjectProjector projector;
    TrackId lows;
    TrackId highs;
};

} // namespace

TEST_CASE("The mix render hears each track before its fader, and the master as it leaves")
{
    MixHarness harness;
    const auto open = harness.measure();
    const auto& lows = open->tracks.at(harness.lows.toString());
    const auto& highs = open->tracks.at(harness.highs.toString());
    MESSAGE("lows " << lows.integratedLufs << " LUFS, highs " << highs.integratedLufs << " LUFS, master "
                    << open->master.integratedLufs << " LUFS, in " << open->renderSeconds << " s");

    // Each track's tone in its own octave.
    CHECK(lows.bandsDb[1] > lows.bandsDb[5] + 30.0);
    CHECK(highs.bandsDb[5] > highs.bandsDb[1] + 30.0);
    CHECK(lows.activeShare > 0.9);

    // A fader moves the master, never the track's own measure.
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackVolume>(harness.highs, -10.0)).ok());
    const auto faded = harness.measure();
    CHECK(std::abs(faded->tracks.at(harness.highs.toString()).integratedLufs - highs.integratedLufs) < 0.05);
    CHECK(faded->master.bandsDb[5] == doctest::Approx(open->master.bandsDb[5] - 10.0).epsilon(0.02));

    // And the Edit that plays never held a tap.
    CHECK(harness.tapsInLiveEdit() == 0);
}

TEST_CASE("A proposal is measured on a copy: its effect is heard there, and nowhere else")
{
    MixHarness harness;
    const auto before = harness.measure();
    const auto projectBefore = harness.state.toValue();

    auto proposed = harness.state;
    PluginInstance eq{};
    eq.id = PluginId::generate();
    eq.ref = PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::equaliser}, "Égaliseur"};
    eq.params = {{std::string{internal::mid1Frequency}, 1000.0},
                 {std::string{internal::mid1Gain}, -6.0},
                 {std::string{internal::mid1Q}, 1.0}};
    std::sort(eq.params.begin(),
              eq.params.end(),
              [](const auto& a, const auto& b) { return a.paramId < b.paramId; });
    REQUIRE(proposed.insertPlugin(harness.highs, eq, 0).ok());

    const auto after = harness.measure(&proposed);
    const auto dip = after->tracks.at(harness.highs.toString()).bandsDb[5] -
                     before->tracks.at(harness.highs.toString()).bandsDb[5];
    MESSAGE("proposed equaliser: " << dip << " dB at 1 kHz on the copy");
    CHECK(dip == doctest::Approx(-6.0).epsilon(0.05));
    CHECK(std::abs(after->tracks.at(harness.lows.toString()).bandsDb[1] -
                   before->tracks.at(harness.lows.toString()).bandsDb[1]) < 0.1);

    // The project and the Edit that plays did not move.
    CHECK(harness.state.toValue() == projectBefore);
    const auto again = harness.measure();
    CHECK(std::abs(again->tracks.at(harness.highs.toString()).bandsDb[5] -
                   before->tracks.at(harness.highs.toString()).bandsDb[5]) < 0.05);
    CHECK(harness.tapsInLiveEdit() == 0);
}
