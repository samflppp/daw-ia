#include "EngineTestSupport.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/engine/ContentStore.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;

// Samples, proved by ear: a sampler channel that plays its sample on the lit
// cells, and an audio clip that sounds at its beat and keeps its length in
// seconds when the tempo changes.

namespace
{

constexpr double hitSampleRate = 44100.0;
constexpr double hitSeconds = 0.05;
constexpr double sampleSeconds = 0.4;

// A short burst then silence: a drum hit, as far as an onset detector cares.
juce::MemoryBlock makeHit()
{
    juce::AudioBuffer<float> buffer{1, static_cast<int>(sampleSeconds * hitSampleRate)};
    buffer.clear();
    const auto loud = static_cast<int>(hitSeconds * hitSampleRate);
    for (int index = 0; index < loud; ++index)
        buffer.setSample(0, index, 0.8f * std::sin(static_cast<float>(index) * 0.2f));

    juce::MemoryBlock bytes;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(stream,
                                          juce::AudioFormatWriterOptions{}
                                              .withSampleRate(hitSampleRate)
                                              .withNumChannels(1)
                                              .withBitsPerSample(16));
        REQUIRE(writer != nullptr);
        REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
    }
    return bytes;
}

// The engine harness, with a content store: samples are read from it.
struct SampleHarness
{
    SampleHarness()
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry}
        , host{"daw_engine_tests"}
        , folder{juce::File::createTempFile("samples")}
        , store{folder.getChildFile("blobs")}
        , projector{host.edit(), state, nullptr, &store}
    {
        bus.addObserver(projector);

        Track track{};
        track.id = trackId;
        track.name = "Kick";
        REQUIRE(state.addTrack(track).ok());
        projector.reconcile();

        const auto bytes = makeHit();
        auto blob = store.put(bytes.getData(), bytes.getSize());
        REQUIRE(blob.ok());

        sample.blob = blob.value();
        sample.name = "Kick.wav";
        sample.format = "wav";
        sample.seconds = sampleSeconds;
    }

    ~SampleHarness() { static_cast<void>(folder.deleteRecursively()); }

    [[nodiscard]] tracktion::AudioTrack* track()
    {
        const auto tracks = tracktion::getAudioTracks(host.edit());
        return tracks.isEmpty() ? nullptr : tracks.getFirst();
    }

    // Every audio clip of the Edit, whichever Tracktion track holds it: the
    // recordings of a domain track live on a companion of its own.
    [[nodiscard]] juce::Array<tracktion::WaveAudioClip*> waves()
    {
        juce::Array<tracktion::WaveAudioClip*> found;
        for (auto* each : tracktion::getAudioTracks(host.edit()))
            found.addArray(tracktion::getClipsOfType<tracktion::WaveAudioClip>(*each));
        return found;
    }

    ProjectState state;
    CommandRegistry registry;
    CommandBus bus;
    daw::engine::EngineHost host;
    juce::File folder;
    daw::engine::ContentStore store;
    daw::engine::ProjectProjector projector;
    TrackId trackId{TrackId::generate()};
    SampleRef sample;
};

// The beats at which something starts sounding, at 120 BPM.
std::vector<int> onsetBeats(tracktion::Edit& edit, double& seconds)
{
    const auto rendered = tracktion::test_utilities::renderToAudioBuffer(edit);
    REQUIRE(rendered.buffer.getNumSamples() > 0);
    seconds = static_cast<double>(rendered.buffer.getNumSamples()) / rendered.sampleRate;

    const auto perBeat = static_cast<int>(std::lround(0.5 * rendered.sampleRate));
    const auto window = perBeat / 8;

    std::vector<float> levels;
    for (int beat = 0; beat * perBeat < rendered.buffer.getNumSamples(); ++beat)
    {
        const auto length = std::min(window, rendered.buffer.getNumSamples() - beat * perBeat);
        levels.push_back(rendered.buffer.getRMSLevel(0, beat * perBeat, length));
    }

    const auto loudest = *std::max_element(levels.begin(), levels.end());
    REQUIRE(loudest > 0.001f);

    std::vector<int> beats;
    for (std::size_t beat = 0; beat < levels.size(); ++beat)
    {
        if (levels[beat] > loudest * 0.3f)
            beats.push_back(static_cast<int>(beat));
    }
    return beats;
}

} // namespace

TEST_CASE("A sampler channel plays its sample on the lit cells, instead of the fallback synth")
{
    SampleHarness harness;
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, harness.sample)).ok());

    auto* track = harness.track();
    REQUIRE(track != nullptr);
    CHECK(track->pluginList.getPluginsOfType<tracktion::SamplerPlugin>().size() == 1);
    CHECK(track->pluginList.getPluginsOfType<tracktion::FourOscPlugin>().isEmpty());

    const auto patternId = PatternId::generate();
    const auto rowId = ClipId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddPatternTrack>(patternId, rowId, harness.trackId)).ok());
    for (const double beat : {0.0, 1.0, 2.0, 3.0})
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = 60;
        note.velocity = 110;
        note.startBeats = beat;
        note.lengthBeats = 0.25;
        REQUIRE(harness.bus.execute(std::make_unique<AddNote>(rowId, note)).ok());
    }
    REQUIRE(
        harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 0.0)).ok());

    double seconds = 0.0;
    const auto beats = onsetBeats(harness.host.edit(), seconds);
    MESSAGE("sampler channel: " << seconds << " s, hits at beats " << beats.size());
    CHECK(beats == std::vector<int>{0, 1, 2, 3});

    // Back to its chain: the sampler leaves, the fallback synth returns.
    REQUIRE(harness.bus.undo().ok()); // placement
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, std::nullopt)).ok());
    CHECK(track->pluginList.getPluginsOfType<tracktion::SamplerPlugin>().isEmpty());
    CHECK(track->pluginList.getPluginsOfType<tracktion::FourOscPlugin>().size() == 1);
}

TEST_CASE("An audio clip sounds at its beat, and a tempo change moves it without stretching it")
{
    SampleHarness harness;
    const auto clipId = AudioClipId::generate();
    REQUIRE(
        harness.bus.execute(std::make_unique<PlaceAudio>(clipId, harness.trackId, harness.sample, 8.0)).ok());

    const auto waves = harness.waves();
    REQUIRE(waves.size() == 1);
    CHECK(waves.getFirst()->getPosition().getStart().inSeconds() == doctest::Approx(4.0));
    CHECK(waves.getFirst()->getPosition().getLength().inSeconds() == doctest::Approx(sampleSeconds));

    double seconds = 0.0;
    const auto beats = onsetBeats(harness.host.edit(), seconds);
    CHECK(beats == std::vector<int>{8});
    CHECK(seconds == doctest::Approx(4.0 + sampleSeconds).epsilon(0.02));

    // At 60 BPM beat 8 is eight seconds in, and the recording still lasts
    // what it lasts: a tempo moves where it starts, never how long it is.
    const auto identity = waves.getFirst()->itemID;
    REQUIRE(harness.bus.execute(std::make_unique<SetTempoPointBpm>(ProjectState::originTempoPointId(), 60.0))
                .ok());

    const auto after = harness.waves();
    REQUIRE(after.size() == 1);
    CHECK(after.getFirst()->itemID == identity);
    CHECK(after.getFirst()->getPosition().getStart().inSeconds() == doctest::Approx(8.0));
    CHECK(after.getFirst()->getPosition().getLength().inSeconds() == doctest::Approx(sampleSeconds));

    // Moved, then removed, through the bus like everything else.
    REQUIRE(harness.bus.execute(std::make_unique<MoveAudio>(clipId, 4.0)).ok());
    CHECK(harness.waves().getFirst()->getPosition().getStart().inSeconds() == doctest::Approx(4.0));

    REQUIRE(harness.bus.execute(std::make_unique<RemoveAudio>(clipId)).ok());
    CHECK(harness.waves().isEmpty());
}

TEST_CASE("Pattern mode leaves the audio clips of the song silent")
{
    SampleHarness harness;
    REQUIRE(harness.bus
                .execute(std::make_unique<PlaceAudio>(
                    AudioClipId::generate(), harness.trackId, harness.sample, 0.0))
                .ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, PatternId{})).ok());

    CHECK(harness.waves().isEmpty());

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::song, PatternId{})).ok());
    CHECK(harness.waves().size() == 1);
}
