#include "EngineTestSupport.h"
#include "TestSettings.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/engine/ContentStore.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;

// The effects of the DAW, proved where they act: in the rendered sound. A
// parameter read back from the Edit would agree with any projection; a
// spectrum does not.

namespace
{

constexpr double sourceRate = 44100.0;
constexpr double sourceSeconds = 4.0;

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

// White noise at -12 dBFS RMS: every band of the spectrum holds energy.
juce::MemoryBlock noise()
{
    juce::AudioBuffer<float> buffer{1, static_cast<int>(sourceSeconds * sourceRate)};
    std::mt19937 random{20};
    std::normal_distribution<float> gauss{0.0f, 0.25f};
    for (int index = 0; index < buffer.getNumSamples(); ++index)
        buffer.setSample(0, index, std::clamp(gauss(random), -0.99f, 0.99f));
    return wavOf(buffer);
}

// Short loud hits over a quiet tone: a high crest factor, which a compressor
// with a fast attack brings down.
juce::MemoryBlock hits()
{
    juce::AudioBuffer<float> buffer{1, static_cast<int>(sourceSeconds * sourceRate)};
    for (int index = 0; index < buffer.getNumSamples(); ++index)
    {
        const auto time = static_cast<double>(index) / sourceRate;
        const auto sinceHit = std::fmod(time, 0.5);
        const auto envelope = sinceHit < 0.05 ? 0.9 : 0.25;
        buffer.setSample(0, index, static_cast<float>(envelope * std::sin(2.0 * 3.14159265 * 220.0 * time)));
    }
    return wavOf(buffer);
}

struct EffectHarness
{
    // The source is played either as a recording on the timeline, or as the
    // sample of a sampler channel held for its whole length: the two roads a
    // sound takes through a track, and an effect has to be on both.
    enum class Road
    {
        recording,
        sampler
    };

    explicit EffectHarness(const juce::MemoryBlock& source, Road road = Road::recording)
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry}
        , host{"daw_engine_tests", daw::testing::engineSettingsFolder()}
        , folder{juce::File::createTempFile("effects")}
        , store{folder.getChildFile("blobs")}
        , projector{host.edit(), state, nullptr, &store}
    {
        bus.addObserver(projector);
        Track track{};
        track.id = trackId;
        track.name = "Source";
        REQUIRE(state.addTrack(track).ok());
        projector.reconcile();

        auto blob = store.put(source.getData(), source.getSize());
        REQUIRE(blob.ok());
        SampleRef sample;
        sample.blob = blob.value();
        sample.name = "source.wav";
        sample.format = "wav";
        sample.seconds = sourceSeconds;
        if (road == Road::recording)
        {
            REQUIRE(bus.execute(std::make_unique<PlaceAudio>(AudioClipId::generate(), trackId, sample, 0.0))
                        .ok());
            return;
        }

        REQUIRE(bus.execute(std::make_unique<SetTrackSample>(trackId, sample)).ok());
        const auto pattern = PatternId::generate();
        const auto row = ClipId::generate();
        const auto beats = sourceSeconds * 2.0; // 120 BPM
        REQUIRE(bus.execute(std::make_unique<CreatePattern>(pattern, "Source", beats)).ok());
        REQUIRE(bus.execute(std::make_unique<AddPatternTrack>(pattern, row, trackId)).ok());
        Note held{};
        held.id = NoteId::generate();
        held.pitch = 60;
        held.velocity = 127;
        held.startBeats = 0.0;
        held.lengthBeats = beats;
        REQUIRE(bus.execute(std::make_unique<AddNote>(row, held)).ok());
        REQUIRE(bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), pattern, 0.0)).ok());
    }

    ~EffectHarness() { static_cast<void>(folder.deleteRecursively()); }

    PluginId insert(std::string_view identifier)
    {
        PluginInstance effect{};
        effect.id = PluginId::generate();
        effect.ref = PluginRef{std::string{PluginRef::internalFormat}, std::string{identifier}, "effet"};
        REQUIRE(bus.execute(std::make_unique<InsertPlugin>(trackId, effect, 0)).ok());
        return effect.id;
    }

    void set(PluginId id, std::string_view parameter, double value)
    {
        REQUIRE(bus.execute(std::make_unique<SetPluginParameter>(id, std::string{parameter}, value)).ok());
    }

    juce::AudioBuffer<float> render()
    {
        auto rendered = tracktion::test_utilities::renderToAudioBuffer(host.edit());
        REQUIRE(rendered.buffer.getNumSamples() > static_cast<int>(sourceRate));
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

// Mean power in a band, in dB, averaged over the whole render with 4096-point
// Hann windows: what the spectrum of the rendered file holds there.
double bandDb(const juce::AudioBuffer<float>& buffer, double rate, double low, double high)
{
    constexpr int order = 12;
    constexpr int size = 1 << order;
    juce::dsp::FFT fft{order};
    juce::dsp::WindowingFunction<float> window{size, juce::dsp::WindowingFunction<float>::hann, false};
    std::vector<float> frame(2 * size);
    double total = 0.0;
    int frames = 0;
    for (int start = size; start + size < buffer.getNumSamples() - size; start += size / 2)
    {
        std::fill(frame.begin(), frame.end(), 0.0f);
        for (int index = 0; index < size; ++index)
            frame[static_cast<std::size_t>(index)] = buffer.getSample(0, start + index);
        window.multiplyWithWindowingTable(frame.data(), size);
        fft.performFrequencyOnlyForwardTransform(frame.data());
        for (int bin = 1; bin < size / 2; ++bin)
        {
            const auto frequency = bin * rate / size;
            if (frequency >= low && frequency < high)
                total += static_cast<double>(frame[static_cast<std::size_t>(bin)]) *
                         frame[static_cast<std::size_t>(bin)];
        }
        ++frames;
    }
    REQUIRE(frames > 0);
    return 10.0 * std::log10(total / frames + 1e-30);
}

// The crest factor the mix measures: the loudest 10 ms against the whole, in
// RMS. Not the sample peak against the RMS: a compressor without look-ahead
// lets the first cycle of a hit through, and a peak-based figure would say
// nothing of what it did to the hit.
double crestFactorDb(const juce::AudioBuffer<float>& buffer)
{
    const auto window = static_cast<int>(0.010 * sourceRate);
    float loudest = 0.0f;
    for (int start = 0; start + window <= buffer.getNumSamples(); start += window)
        loudest = std::max(loudest, buffer.getRMSLevel(0, start, window));
    const auto rms = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    REQUIRE(rms > 0.0f);
    return 20.0 * std::log10(loudest / rms);
}

} // namespace

TEST_CASE(
    "The equaliser of the DAW: a -6 dB bell at 1 kHz is a 6 dB dip in the rendered spectrum, there only")
{
    for (const auto road : {EffectHarness::Road::recording, EffectHarness::Road::sampler})
    {
        CAPTURE(road == EffectHarness::Road::recording ? "recording" : "sampler");
        EffectHarness harness{noise(), road};
        const auto flat = harness.render();

        const auto eq = harness.insert(internal::equaliser);
        harness.set(eq, internal::mid1Frequency, 1000.0);
        harness.set(eq, internal::mid1Gain, -6.0);
        harness.set(eq, internal::mid1Q, 4.0);
        const auto dipped = harness.render();

        const auto rate = harness.renderedRate;
        const auto at1k = bandDb(dipped, rate, 970.0, 1030.0) - bandDb(flat, rate, 970.0, 1030.0);
        const auto at100 = bandDb(dipped, rate, 90.0, 110.0) - bandDb(flat, rate, 90.0, 110.0);
        const auto at8k = bandDb(dipped, rate, 7500.0, 8500.0) - bandDb(flat, rate, 7500.0, 8500.0);
        MESSAGE("rendered: " << at1k << " dB at 1 kHz, " << at100 << " dB at 100 Hz, " << at8k
                             << " dB at 8 kHz");
        CHECK(at1k == doctest::Approx(-6.0).epsilon(0.08));
        CHECK(std::abs(at100) < 0.3);
        CHECK(std::abs(at8k) < 0.3);

        // The curve the screen draws is the one the render measured.
        const auto* drawn = harness.state.findPlugin(eq);
        REQUIRE(drawn != nullptr);
        CHECK(std::abs(equaliserGainDb(*drawn, 1000.0, rate) - at1k) < 0.5);

        // Bypassed, it does nothing.
        REQUIRE(harness.bus.execute(std::make_unique<SetPluginBypassed>(eq, true)).ok());
        const auto bypassed = harness.render();
        CHECK(std::abs(bandDb(bypassed, rate, 970.0, 1030.0) - bandDb(flat, rate, 970.0, 1030.0)) < 0.3);
    }
}

TEST_CASE("The high-pass of the DAW's equaliser empties the low end of the render, and is off at 20 Hz")
{
    EffectHarness harness{noise()};
    const auto flat = harness.render();
    const auto rate = harness.renderedRate;

    const auto eq = harness.insert(internal::equaliser);
    harness.set(eq, internal::highPassFrequency, internal::highPassOff);
    const auto off = harness.render();
    CHECK(std::abs(bandDb(off, rate, 25.0, 45.0) - bandDb(flat, rate, 25.0, 45.0)) < 0.3);

    harness.set(eq, internal::highPassFrequency, 300.0);
    const auto cut = harness.render();
    const auto at40 = bandDb(cut, rate, 30.0, 50.0) - bandDb(flat, rate, 30.0, 50.0);
    const auto at3k = bandDb(cut, rate, 2800.0, 3200.0) - bandDb(flat, rate, 2800.0, 3200.0);
    MESSAGE("high-pass at 300 Hz: " << at40 << " dB at 40 Hz, " << at3k << " dB at 3 kHz");
    CHECK(at40 < -30.0);
    CHECK(std::abs(at3k) < 0.3);
}

TEST_CASE("The compressor of the DAW lowers the crest factor of what it renders")
{
    EffectHarness harness{hits()};
    const auto open = harness.render();
    const auto before = crestFactorDb(open);

    const auto comp = harness.insert(internal::compressor);
    harness.set(comp, internal::threshold, -12.0);
    harness.set(comp, internal::ratio, 8.0);
    harness.set(comp, internal::attack, 0.3);
    harness.set(comp, internal::release, 50.0);
    const auto squeezed = harness.render();
    const auto after = crestFactorDb(squeezed);
    MESSAGE("crest factor: " << before << " dB open, " << after << " dB compressed");
    CHECK(after < before - 3.0);

    // A gentle ratio does less than a hard one: the effect follows the number.
    harness.set(comp, internal::ratio, 1.5);
    const auto gentle = crestFactorDb(harness.render());
    MESSAGE("crest factor at 1.5:1: " << gentle << " dB");
    CHECK(gentle > after + 1.0);
}

// plugin.move (S24): the engine moves the plugins it holds, it does not make
// them again; and what is heard is the chain in its new order.
TEST_CASE(
    "A plugin moved in its chain is the same plugin in the engine, and the render follows its new place")
{
    // A track that plays a recording carries its chain twice, on itself and
    // on its companion: each one is looked at.
    const auto partsIn = [](tracktion::AudioTrack& track, PluginId id)
    {
        std::vector<tracktion::Plugin*> parts;
        for (auto* plugin : track.pluginList.getPlugins())
            if (plugin->state.getProperty("dawDomainPluginId").toString() == juce::String(id.toString()))
                parts.push_back(plugin);
        return parts;
    };

    // A high shelf of +12 dB and a hard compressor: before the compressor the
    // boost is squeezed, after it the boost is heard whole.
    EffectHarness moved{noise()};
    const auto eq = moved.insert(internal::equaliser);
    moved.set(eq, internal::highFrequency, 1000.0);
    moved.set(eq, internal::highGain, 12.0);
    const auto comp = moved.insert(internal::compressor); // inserted at 0: in front of the equaliser
    moved.set(comp, internal::threshold, -30.0);
    moved.set(comp, internal::ratio, 8.0);
    const auto before = moved.render();

    struct Held
    {
        tracktion::AudioTrack* track;
        std::vector<tracktion::Plugin*> eq;
        std::vector<tracktion::Plugin*> comp;
    };
    std::vector<Held> held;
    for (auto* track : tracktion::getAudioTracks(moved.host.edit()))
    {
        auto eqParts = partsIn(*track, eq);
        auto compParts = partsIn(*track, comp);
        if (eqParts.empty() && compParts.empty())
            continue;
        REQUIRE(eqParts.size() == 2); // the high-pass and the four bands
        REQUIRE(compParts.size() == 1);
        CHECK(track->pluginList.indexOf(compParts.front()) < track->pluginList.indexOf(eqParts.front()));
        held.push_back({track, eqParts, compParts});
    }
    REQUIRE_FALSE(held.empty());

    REQUIRE(moved.bus.execute(std::make_unique<MovePlugin>(comp, 1)).ok());
    for (const auto& chain : held)
    {
        const auto& list = chain.track->pluginList;
        CHECK(partsIn(*chain.track, eq) == chain.eq);
        CHECK(partsIn(*chain.track, comp) == chain.comp);
        CHECK(list.indexOf(chain.eq.back()) < list.indexOf(chain.comp.front()));
        CHECK(list.indexOf(chain.eq.front()) + 1 == list.indexOf(chain.eq.back()));
    }
    const auto after = moved.render();

    // The same chain, made directly in the new order.
    EffectHarness made{noise()};
    const auto comp2 = made.insert(internal::compressor);
    made.set(comp2, internal::threshold, -30.0);
    made.set(comp2, internal::ratio, 8.0);
    const auto eq2 = made.insert(internal::equaliser); // at 0: in front
    made.set(eq2, internal::highFrequency, 1000.0);
    made.set(eq2, internal::highGain, 12.0);
    const auto expected = made.render();

    const auto rms = [](const juce::AudioBuffer<float>& buffer)
    { return buffer.getRMSLevel(0, 0, buffer.getNumSamples()); };
    REQUIRE(after.getNumSamples() == expected.getNumSamples());
    juce::AudioBuffer<float> difference{after};
    difference.addFrom(0, 0, expected, 0, 0, expected.getNumSamples(), -1.0f);
    const auto apart = 20.0 * std::log10(rms(difference) / rms(expected) + 1e-12);
    juce::AudioBuffer<float> reordered{after};
    reordered.addFrom(0, 0, before, 0, 0, before.getNumSamples(), -1.0f);
    const auto orders = 20.0 * std::log10(rms(reordered) / rms(after) + 1e-12);
    MESSAGE("moved against made in that order: " << apart << " dB ; the two orders differ by " << orders
                                                 << " dB");
    CHECK(apart < -60.0);
    CHECK(orders > -20.0);
}
