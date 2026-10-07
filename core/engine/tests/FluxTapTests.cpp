#include "EngineTestSupport.h"
#include "TestSettings.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/FluxTap.h"
#include "daw/engine/FluxTaps.h"
#include "daw/engine/MeterTap.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::engine::FluxTapPlugin;

// The taps of the audio flux (S24): where the projection puts them, and what
// they hold, proved against the render — a waveform read at one place of a
// chain is the sound that the chain makes there.

namespace
{

constexpr double sourceRate = 44100.0;
constexpr double sourceSeconds = 3.0;

juce::MemoryBlock wavOf(const juce::AudioBuffer<float>& buffer)
{
    juce::MemoryBlock bytes;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream,
                                          juce::AudioFormatWriterOptions{}
                                              .withSampleRate(sourceRate)
                                              .withNumChannels(1)
                                              .withBitsPerSample(24));
        REQUIRE(writer != nullptr);
        REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
    }
    return bytes;
}

juce::AudioBuffer<float> noiseBuffer()
{
    juce::AudioBuffer<float> buffer{1, static_cast<int>(sourceSeconds * sourceRate)};
    std::mt19937 random{24};
    std::normal_distribution<float> gauss{0.0f, 0.25f};
    for (int index = 0; index < buffer.getNumSamples(); ++index)
        buffer.setSample(0, index, std::clamp(gauss(random), -0.99f, 0.99f));
    return buffer;
}

// Silence, and one sample at 0.8 one second in: where it lands says when.
juce::AudioBuffer<float> clickBuffer()
{
    juce::AudioBuffer<float> buffer{1, static_cast<int>(sourceSeconds * sourceRate)};
    buffer.clear();
    buffer.setSample(0, static_cast<int>(sourceRate), 0.8f);
    return buffer;
}

struct Harness
{
    explicit Harness(const juce::AudioBuffer<float>& source)
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry}
        , host{"daw_engine_tests", daw::testing::engineSettingsFolder()}
        , folder{juce::File::createTempFile("flux")}
        , store{folder.getChildFile("blobs")}
        , projector{host.edit(), state, nullptr, &store}
    {
        bus.addObserver(projector);
        Track track{};
        track.id = trackId;
        track.name = "Source";
        REQUIRE(state.addTrack(track).ok());
        projector.reconcile();

        const auto bytes = wavOf(source);
        auto blob = store.put(bytes.getData(), bytes.getSize());
        REQUIRE(blob.ok());
        SampleRef sample;
        sample.blob = blob.value();
        sample.name = "source.wav";
        sample.format = "wav";
        sample.seconds = sourceSeconds;
        REQUIRE(
            bus.execute(std::make_unique<PlaceAudio>(AudioClipId::generate(), trackId, sample, 0.0)).ok());
    }

    ~Harness() { static_cast<void>(folder.deleteRecursively()); }

    PluginId insert(std::string_view identifier, std::size_t index)
    {
        PluginInstance effect{};
        effect.id = PluginId::generate();
        effect.ref = PluginRef{std::string{PluginRef::internalFormat}, std::string{identifier}, "effet"};
        REQUIRE(bus.execute(std::make_unique<InsertPlugin>(trackId, effect, index)).ok());
        return effect.id;
    }

    void set(PluginId id, std::string_view parameter, double value)
    {
        REQUIRE(bus.execute(std::make_unique<SetPluginParameter>(id, std::string{parameter}, value)).ok());
    }

    // The companion: the track that plays the recordings, where the taps of
    // this track's recorded sound are.
    tracktion::AudioTrack& companion()
    {
        tracktion::AudioTrack* found = nullptr;
        for (auto* track : tracktion::getAudioTracks(host.edit()))
            for (auto* tap : track->pluginList.getPluginsOfType<FluxTapPlugin>())
                if (tap->companion())
                    found = track;
        REQUIRE(found != nullptr);
        return *found;
    }

    FluxTapPlugin& tap(const juce::String& slot, bool onCompanion = true)
    {
        for (auto* track : tracktion::getAudioTracks(host.edit()))
            for (auto* each : track->pluginList.getPluginsOfType<FluxTapPlugin>())
                if (each->slot() == slot && each->companion() == onCompanion &&
                    each->strip() == juce::String(trackId.toString()))
                    return *each;
        FAIL("no tap " << slot);
        throw std::logic_error{"unreachable"};
    }

    // Renders with every tap of the companion capturing.
    juce::AudioBuffer<float> render()
    {
        for (auto* each : companion().pluginList.getPluginsOfType<FluxTapPlugin>())
            each->startCapture(static_cast<int>((sourceSeconds + 1.0) * 96000.0));
        auto rendered = tracktion::test_utilities::renderToAudioBuffer(host.edit());
        for (auto* each : companion().pluginList.getPluginsOfType<FluxTapPlugin>())
            each->stopCapture();
        renderedRate = rendered.sampleRate;
        return rendered.buffer;
    }

    ProjectState state;
    CommandRegistry registry;
    CommandBus bus;
    daw::engine::EngineHost host;
    juce::File folder;
    daw::engine::ContentStore store;
    daw::engine::ProjectProjector projector;
    TrackId trackId{TrackId::generate()};
    double renderedRate{sourceRate};
};

// The mono mean of a render's channels, as a tap holds it.
std::vector<float> monoOf(const juce::AudioBuffer<float>& buffer)
{
    std::vector<float> mono(static_cast<std::size_t>(buffer.getNumSamples()), 0.0f);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = 0; index < buffer.getNumSamples(); ++index)
            mono[static_cast<std::size_t>(index)] +=
                buffer.getSample(channel, index) / static_cast<float>(buffer.getNumChannels());
    return mono;
}

// How far `measured` is from `expected` times the best gain, against
// `measured`, in dB; and that gain.
std::pair<double, double> apart(const std::vector<float>& measured, const std::vector<float>& expected)
{
    const auto count = std::min(measured.size(), expected.size());
    double cross = 0.0;
    double energy = 0.0;
    double own = 0.0;
    for (std::size_t index = 0; index < count; ++index)
    {
        cross += static_cast<double>(measured[index]) * expected[index];
        energy += static_cast<double>(expected[index]) * expected[index];
        own += static_cast<double>(measured[index]) * measured[index];
    }
    const auto gain = energy > 0.0 ? cross / energy : 0.0;
    double residual = 0.0;
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto difference = measured[index] - gain * expected[index];
        residual += difference * difference;
    }
    return {10.0 * std::log10(residual / std::max(own, 1e-30) + 1e-30), gain};
}

std::size_t loudestAt(const std::vector<float>& samples)
{
    std::size_t best = 0;
    for (std::size_t index = 0; index < samples.size(); ++index)
        if (std::abs(samples[index]) > std::abs(samples[best]))
            best = index;
    return best;
}

} // namespace

TEST_CASE("Each state of a chain has its tap, in place: the source, after each effect, after the fader")
{
    Harness harness{noiseBuffer()};
    const auto eq = harness.insert(internal::equaliser, 0);
    const auto comp = harness.insert(internal::compressor, 1);

    auto& list = harness.companion().pluginList;
    const auto indexOfTap = [&](const juce::String& slot)
    {
        for (auto* tap : list.getPluginsOfType<FluxTapPlugin>())
            if (tap->slot() == slot)
                return list.indexOf(tap);
        return -1;
    };
    const auto partsOf = [&](PluginId id)
    {
        std::vector<int> at;
        for (auto* plugin : list.getPlugins())
            if (plugin->state.getProperty("dawDomainPluginId").toString() == juce::String(id.toString()))
                at.push_back(list.indexOf(plugin));
        return at;
    };

    const auto eqParts = partsOf(eq);
    const auto compParts = partsOf(comp);
    REQUIRE(eqParts.size() == 2);
    REQUIRE(compParts.size() == 1);
    CHECK(indexOfTap(FluxTapPlugin::sourceSlot) + 1 == eqParts.front());
    CHECK(indexOfTap(juce::String(eq.toString())) == eqParts.back() + 1);
    CHECK(indexOfTap(juce::String(eq.toString())) + 1 == compParts.front());
    CHECK(indexOfTap(juce::String(comp.toString())) == compParts.back() + 1);
    auto* volume = harness.companion().getVolumePlugin();
    REQUIRE(volume != nullptr);
    CHECK(indexOfTap(FluxTapPlugin::faderSlot) == list.indexOf(volume) + 1);
    CHECK(list.getPlugins().getLast()->getPluginType() ==
          juce::String(daw::engine::MeterTapPlugin::xmlTypeName));

    // An effect taken out takes its tap with it; one moved, its tap follows.
    REQUIRE(harness.bus.execute(std::make_unique<MovePlugin>(comp, 0)).ok());
    CHECK(indexOfTap(juce::String(comp.toString())) == partsOf(comp).back() + 1);
    CHECK(indexOfTap(juce::String(comp.toString())) + 1 == partsOf(eq).front());
    REQUIRE(harness.bus.execute(std::make_unique<RemovePlugin>(eq)).ok());
    CHECK(indexOfTap(juce::String(eq.toString())) == -1);
    CHECK(list.getPluginsOfType<FluxTapPlugin>().size() == 3); // source, the compressor, the fader
}

TEST_CASE("A bus is tapped at its sum and after its fader, the master too")
{
    Harness harness{noiseBuffer()};
    const auto drums = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddBus>(drums, "Drums")).ok());
    PluginInstance comp{};
    comp.id = PluginId::generate();
    comp.ref = PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::compressor}, "comp"};
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(drums, comp, 0)).ok());

    bool busSum = false;
    for (auto* track : tracktion::getAudioTracks(harness.host.edit()))
        for (auto* tap : track->pluginList.getPluginsOfType<FluxTapPlugin>())
            if (tap->strip() == juce::String(drums.toString()) && tap->slot() == FluxTapPlugin::sourceSlot)
                busSum = track->pluginList.indexOf(tap) == 1; // after the aux return
    CHECK(busSum);

    auto& master = harness.host.edit().getMasterPluginList();
    const auto taps = master.getPluginsOfType<FluxTapPlugin>();
    REQUIRE(taps.size() == 2);
    CHECK(master.indexOf(taps.getFirst()) == 0);
    CHECK(taps.getFirst()->slot() == FluxTapPlugin::sourceSlot);
    CHECK(taps.getLast()->slot() == FluxTapPlugin::faderSlot);
}

TEST_CASE("Six effects on the master are all there: Tracktion's limit of four plugins is lifted")
{
    Harness harness{noiseBuffer()};
    for (int index = 0; index < 6; ++index)
    {
        PluginInstance comp{};
        comp.id = PluginId::generate();
        comp.ref =
            PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::compressor}, "comp"};
        REQUIRE(
            harness.bus.execute(std::make_unique<InsertPlugin>(ProjectState::masterTrackId(), comp, 0)).ok());
    }
    auto& master = harness.host.edit().getMasterPluginList();
    CHECK(master.getPluginsOfType<tracktion::CompressorPlugin>().size() == 6);
    CHECK(master.getPluginsOfType<FluxTapPlugin>().size() == 8);
}

TEST_CASE("A tap's waveform is the render at its place: the sound after the equaliser, sample by sample")
{
    // The chain: equaliser (a high-pass at 1 kHz), then a hard compressor.
    Harness full{noiseBuffer()};
    const auto eq = full.insert(internal::equaliser, 0);
    full.set(eq, internal::highPassFrequency, 1000.0);
    const auto comp = full.insert(internal::compressor, 1);
    full.set(comp, internal::threshold, -30.0);
    full.set(comp, internal::ratio, 8.0);
    static_cast<void>(full.render());
    const auto& afterEq = full.tap(juce::String(eq.toString())).captured();
    const auto& afterComp = full.tap(juce::String(comp.toString())).captured();
    const auto& source = full.tap(FluxTapPlugin::sourceSlot).captured();

    // The render of the chain cut after the equaliser.
    Harness cut{noiseBuffer()};
    const auto eq2 = cut.insert(internal::equaliser, 0);
    cut.set(eq2, internal::highPassFrequency, 1000.0);
    const auto rendered = monoOf(cut.render());

    const auto length = static_cast<std::size_t>(sourceSeconds * full.renderedRate) - 64;
    const std::vector<float> tapped(afterEq.begin(), afterEq.begin() + static_cast<std::ptrdiff_t>(length));
    const auto [eqApart, eqGain] = apart(tapped, rendered);
    MESSAGE("after the equaliser, against the render cut there: " << eqApart << " dB, gain " << eqGain);
    CHECK(eqApart < -60.0);

    // The source is the render of the recording with no effect at all (the
    // render runs at the device's rate, and resamples the file).
    Harness bare{noiseBuffer()};
    const auto plain = monoOf(bare.render());
    const std::vector<float> fromSource(source.begin(), source.begin() + static_cast<std::ptrdiff_t>(length));
    const auto [sourceApart, sourceGain] = apart(fromSource, plain);
    MESSAGE("source, against the render without effects: " << sourceApart << " dB, gain " << sourceGain);
    CHECK(sourceApart < -60.0);
    CHECK(sourceGain == doctest::Approx(eqGain).epsilon(0.01)); // the same pan law

    // After the compressor, the level has changed: another gain against the
    // same render.
    const std::vector<float> compressed(afterComp.begin(),
                                        afterComp.begin() + static_cast<std::ptrdiff_t>(length));
    const auto compGain = apart(compressed, rendered).second;
    MESSAGE("after the compressor, gain against the equaliser's render " << compGain);
    CHECK(compGain < 0.8 * eqGain);
}

TEST_CASE("A bypassed effect gives the same waveform before and after it")
{
    Harness harness{noiseBuffer()};
    const auto eq = harness.insert(internal::equaliser, 0);
    harness.set(eq, internal::highPassFrequency, 1000.0);
    REQUIRE(harness.bus.execute(std::make_unique<SetPluginBypassed>(eq, true)).ok());
    static_cast<void>(harness.render());
    const auto& before = harness.tap(FluxTapPlugin::sourceSlot).captured();
    const auto& after = harness.tap(juce::String(eq.toString())).captured();
    const auto length = static_cast<std::size_t>(sourceSeconds * harness.renderedRate);
    const std::vector<float> a(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(length));
    const std::vector<float> b(after.begin(), after.begin() + static_cast<std::ptrdiff_t>(length));
    const auto [difference, gain] = apart(b, a);
    MESSAGE("bypassed: after against before " << difference << " dB, gain " << gain);
    CHECK(difference < -100.0);
    CHECK(gain == doctest::Approx(1.0).epsilon(1e-6));
}

TEST_CASE("A plugin that delays the sound: the before and the after of it read at the same instant")
{
    Harness harness{clickBuffer()};
    const auto eq = harness.insert(internal::equaliser, 0); // flat: it passes the click through

    // Tracktion's latency tester, 50 ms, put by hand right after the source
    // tap: it delays the sound and says by how much.
    auto& list = harness.companion().pluginList;
    harness.host.engine().getPluginManager().createBuiltInType<tracktion::LatencyPlugin>();
    auto latency = harness.host.edit().getPluginCache().createNewPlugin(tracktion::LatencyPlugin::create());
    REQUIRE(latency != nullptr);
    latency->state.setProperty(tracktion::IDs::time, 0.05, nullptr);
    const auto at = list.indexOf(&harness.tap(FluxTapPlugin::sourceSlot));
    list.insertPlugin(latency, at + 1, nullptr);
    REQUIRE(latency->getLatencySeconds() == doctest::Approx(0.05));

    static_cast<void>(harness.render());
    const auto& before = harness.tap(FluxTapPlugin::sourceSlot).captured();
    const auto& after = harness.tap(juce::String(eq.toString())).captured();
    const auto clickBefore = loudestAt(before);
    const auto clickAfter = loudestAt(after);
    MESSAGE("the click: at " << clickBefore << " before the delay, at " << clickAfter << " after it ("
                             << harness.renderedRate * 0.05 << " samples of latency)");
    CHECK(std::abs(before[clickBefore]) > 0.5f);
    CHECK(std::abs(after[clickAfter]) > 0.5f);
    CHECK(clickBefore == clickAfter);
}

TEST_CASE("Reconciling again touches no plugin: the same objects in the same places, on every list")
{
    Harness harness{noiseBuffer()};
    const auto drums = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddBus>(drums, "Drums")).ok());
    static_cast<void>(harness.insert(internal::equaliser, 0));
    static_cast<void>(harness.insert(internal::compressor, 1));
    PluginInstance comp{};
    comp.id = PluginId::generate();
    comp.ref = PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::compressor}, "comp"};
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(ProjectState::masterTrackId(), comp, 0)).ok());

    // A plugin made again, or moved, rebuilds the playback graph: what plays
    // stops for a moment. A reconcile that changes nothing must touch nothing.
    const auto snapshot = [&]
    {
        std::vector<tracktion::Plugin*> plugins;
        for (auto* track : tracktion::getAudioTracks(harness.host.edit()))
            for (auto* plugin : track->pluginList.getPlugins())
                plugins.push_back(plugin);
        for (auto* plugin : harness.host.edit().getMasterPluginList().getPlugins())
            plugins.push_back(plugin);
        return plugins;
    };
    const auto before = snapshot();
    harness.projector.reconcile();
    harness.projector.reconcile();
    CHECK(snapshot() == before);
}

TEST_CASE("The flux reads a place by position: what its armed ring holds is what the render made there")
{
    Harness harness{noiseBuffer()};
    const auto eq = harness.insert(internal::equaliser, 0);
    daw::engine::FluxTaps taps{harness.host.edit()};
    const daw::engine::FluxTaps::Place source{harness.trackId.toString(), "source"};
    const daw::engine::FluxTaps::Place afterEq{harness.trackId.toString(), eq.toString()};

    // The source armed — on the channel's track and on its companion —, the
    // equaliser's place not.
    taps.arm({source});
    CHECK(taps.tapsAt(source).size() == 2);
    for (auto* tap : taps.all())
        CHECK(tap->armed() ==
              (tap->slot() == "source" && tap->strip() == juce::String(harness.trackId.toString())));

    static_cast<void>(harness.render());
    const auto last = taps.latest();
    REQUIRE(last > 48000);

    // The last tenth of a second, read by position: the two taps added (the
    // channel's instrument plays nothing here), equal to the capture.
    constexpr int count = 4800;
    std::vector<float> read(count);
    taps.read(source, last - count + 1, count, read.data());
    const auto& captured = harness.tap(FluxTapPlugin::sourceSlot).captured();
    REQUIRE(static_cast<std::size_t>(last) < captured.size());
    double heard = 0.0;
    for (int index = 0; index < count; ++index)
    {
        const auto position = static_cast<std::size_t>(last - count + 1 + index);
        CHECK(read[static_cast<std::size_t>(index)] == doctest::Approx(captured[position]).epsilon(1e-6));
        heard = std::max(heard, static_cast<double>(std::abs(read[static_cast<std::size_t>(index)])));
    }
    CHECK(heard > 0.1);

    // A place not armed holds nothing.
    taps.read(afterEq, last - count + 1, count, read.data());
    CHECK(std::all_of(read.begin(), read.end(), [](float sample) { return sample == 0.0f; }));
}

TEST_CASE("Listening alone at a place: the master plays that place, mono, in place of the mix")
{
    Harness harness{noiseBuffer()};
    const auto eq = harness.insert(internal::equaliser, 0);
    harness.set(eq, internal::highPassFrequency, 1000.0);
    daw::engine::FluxTaps taps{harness.host.edit()};
    const daw::engine::FluxTaps::Place source{harness.trackId.toString(), "source"};

    // An offline render plays the song, never the place: a test asks for it.
    taps.listen(source, true);
    const auto heard = monoOf(harness.render());
    taps.listen(std::nullopt);
    const auto length = static_cast<std::size_t>(sourceSeconds * harness.renderedRate) - 64;
    const auto& captured = harness.tap(FluxTapPlugin::sourceSlot).captured();
    // A render stops at full scale: the source, resampled, goes past it once.
    std::vector<float> fromSource(captured.begin(), captured.begin() + static_cast<std::ptrdiff_t>(length));
    for (auto& sample : fromSource)
        sample = std::clamp(sample, -1.0f, 1.0f);
    const auto& afterEq = harness.tap(juce::String(eq.toString())).captured();
    const std::vector<float> filtered(afterEq.begin(), afterEq.begin() + static_cast<std::ptrdiff_t>(length));

    const auto [apartSource, gain] = apart(
        std::vector<float>(heard.begin(), heard.begin() + static_cast<std::ptrdiff_t>(length)), fromSource);
    MESSAGE("listened at the source, against its tap: " << apartSource << " dB, gain " << gain);
    CHECK(apartSource < -100.0);
    CHECK(gain == doctest::Approx(1.0).epsilon(0.001));
    const auto apartFiltered =
        apart(std::vector<float>(heard.begin(), heard.begin() + static_cast<std::ptrdiff_t>(length)),
              filtered)
            .first;
    CHECK(apartFiltered > -10.0); // not the equaliser's sound

    // Stopped: the song again, the equaliser in it; and an offline render
    // while listening, without the test's leave, is the song too.
    const auto loudest = [](const std::vector<float>& samples)
    {
        float peak = 0.0f;
        for (const auto sample : samples)
            peak = std::max(peak, std::abs(sample));
        return peak;
    };
    const auto song = monoOf(harness.render());
    CHECK(loudest(song) > 0.1f);
    CHECK(
        apart(std::vector<float>(song.begin(), song.begin() + static_cast<std::ptrdiff_t>(length)), filtered)
            .first < -60.0);
    taps.listen(source);
    const auto exported = monoOf(harness.render());
    CHECK(loudest(exported) > 0.1f);
    taps.listen(std::nullopt);
    CHECK(apart(std::vector<float>(exported.begin(), exported.begin() + static_cast<std::ptrdiff_t>(length)),
                filtered)
              .first < -60.0);
}
