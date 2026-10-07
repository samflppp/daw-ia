#include "EngineTestSupport.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/live/Router.h"
#include "daw/domain/serialization/Json.h"
#include "daw/domain/sound/Pitch.h"
#include "daw/engine/LiveInput.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::domain::live::Router;
using daw::engine::LiveInputPlugin;
using daw::testing::EngineHarness;

// Playing live (S23), proven by the sound. Notes are pushed into the router
// the way a keyboard pushes them, stamped with the instants of the render at
// which they are played (LiveInputPlugin::Clock::edit), and the Edit is
// rendered: where the sound starts, which pitch it has, which track it comes
// from, and that it stops — measured in the file, never in a counter of
// messages.

namespace
{

constexpr double rate = 44100.0;
constexpr int block = 256;
constexpr int sim = Router::simulation;

// The clock of the plugin set for one test, and put back after.
struct EditClock
{
    EditClock() { LiveInputPlugin::setClock(LiveInputPlugin::Clock::edit); }
    ~EditClock() { LiveInputPlugin::setClock(LiveInputPlugin::Clock::input); }
    EditClock(const EditClock&) = delete;
    EditClock& operator=(const EditClock&) = delete;
    EditClock(EditClock&&) = delete;
    EditClock& operator=(EditClock&&) = delete;
};

// A harness whose projector plays live from the host's router.
struct LiveHarness : EngineHarness
{
    LiveHarness()
    {
        projector.playLiveFrom(&host.live());
        projector.reconcile();
        host.live().setTarget(trackId.toString(), 0.0);
    }

    [[nodiscard]] Router& router() { return host.live(); }
};

// `seconds` of the Edit, rendered block by block; `during` is told the
// time reached after each block, which lets a test act in the middle.
tracktion::test_utilities::BufferAndSampleRate render(tracktion::Edit& edit,
                                                      double seconds,
                                                      const std::function<void(double)>& during = {},
                                                      int blockSize = block)
{
    auto file = std::make_unique<juce::TemporaryFile>(".wav");
    tracktion::Renderer::Parameters parameters{edit};
    parameters.destFile = file->getFile();
    parameters.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    parameters.bitDepth = 32;
    parameters.sampleRateForAudio = rate;
    parameters.blockSizeForAudio = blockSize;
    parameters.time =
        tracktion::TimeRange{tracktion::TimePosition{}, tracktion::TimePosition::fromSeconds(seconds)};
    parameters.tracksToDo = tracktion::toBitSet(tracktion::getAllTracks(edit));
    parameters.usePlugins = true;
    parameters.useMasterPlugins = true;
    parameters.canRenderInMono = false;

    tracktion::TransportControl::stopAllTransports(edit.engine, false, true);
    const tracktion::TransportControl::ScopedContextAllocator restore{edit.getTransport()};
    edit.getTransport().freePlaybackContext();
    tracktion::Renderer::turnOffAllPlugins(edit);

    auto task = tracktion::render_utils::createRenderTask(parameters, "jeu", nullptr, nullptr);
    REQUIRE(task != nullptr);
    while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
    {
        if (during)
            during(static_cast<double>(task->getCurrentTaskProgress()) * seconds);
    }
    task.reset();
    tracktion::Renderer::turnOffAllPlugins(edit);
    return tracktion::test_utilities::loadBufferAndSampleRate(std::move(file));
}

int sampleAt(double seconds)
{
    return static_cast<int>(std::lround(seconds * rate));
}

// The loudest channel's RMS over [from, to), dBFS.
double levelDb(const juce::AudioBuffer<float>& audio, double from, double to, int channel = -1)
{
    const auto start = std::clamp(sampleAt(from), 0, audio.getNumSamples());
    const auto end = std::clamp(sampleAt(to), start, audio.getNumSamples());
    if (end <= start)
        return -200.0;
    float rms = 0.0f;
    for (int index = 0; index < audio.getNumChannels(); ++index)
    {
        if (channel < 0 || index == channel)
            rms = std::max(rms, audio.getRMSLevel(index, start, end - start));
    }
    return rms > 0.0f ? 20.0 * std::log10(static_cast<double>(rms)) : -200.0;
}

// Where the sound first rises above -60 dBFS, in seconds; -1 when never.
double onset(const juce::AudioBuffer<float>& audio)
{
    for (int index = 0; index < audio.getNumSamples(); ++index)
    {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        {
            if (std::abs(audio.getSample(channel, index)) > 0.001f)
                return index / rate;
        }
    }
    return -1.0;
}

int pitchOver(const juce::AudioBuffer<float>& audio, double from, double to)
{
    const auto start = sampleAt(from);
    const auto hertz = daw::domain::sound::fundamentalOf(
        audio.getReadPointer(0, start), sampleAt(to) - start, rate, 30.0, 2000.0);
    return hertz > 0.0 ? daw::domain::sound::midiPitchOf(hertz) : -1;
}

constexpr double silentDb = -90.0;
constexpr double heardDb = -40.0;

} // namespace

TEST_CASE("Live: a note played sounds at its pitch, when it was played, and writes nothing")
{
    const EditClock clock;
    LiveHarness harness;
    const auto before = json::write(harness.state.toValue());
    const auto history = harness.bus.journal().size();

    REQUIRE(harness.router().noteOn(sim, 1, 69, 100, 0.5));
    REQUIRE(harness.router().noteOff(sim, 1, 69, 1.0));
    const auto rendered = render(harness.host.edit(), 2.5);
    const auto& audio = rendered.buffer;

    CHECK(levelDb(audio, 0.0, 0.49) < silentDb);
    const auto start = onset(audio);
    MESSAGE("première trace audible à " << start << " s, jouée à 0,5 s");
    CHECK(start >= 0.5);
    CHECK(start < 0.5 + 0.003);
    CHECK(levelDb(audio, 0.55, 0.95) > heardDb);
    CHECK(pitchOver(audio, 0.6, 0.9) == 69);
    CHECK(levelDb(audio, 2.0, 2.5) < silentDb);

    // Nothing in the project, nothing in its history.
    CHECK(json::write(harness.state.toValue()) == before);
    CHECK(harness.bus.journal().size() == history);
}

TEST_CASE("Live: the chosen track plays, and only it")
{
    const EditClock clock;
    LiveHarness harness;

    // A second track, panned to the right; the first, to the left.
    const auto other = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(other, "Piste 2")).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackPan>(harness.trackId, -1.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackPan>(other, 1.0)).ok());

    harness.router().setTarget(other.toString(), 0.0);
    REQUIRE(harness.router().noteOn(sim, 1, 60, 100, 0.2));
    REQUIRE(harness.router().noteOff(sim, 1, 60, 0.8));
    const auto rendered = render(harness.host.edit(), 1.5);

    CHECK(levelDb(rendered.buffer, 0.3, 0.7, 1) > heardDb);  // right: the chosen one
    CHECK(levelDb(rendered.buffer, 0.3, 0.7, 0) < silentDb); // left: the other
}

TEST_CASE("Live: a key released after the track changed silences the track it played")
{
    const EditClock clock;
    LiveHarness harness;
    const auto other = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(other, "Piste 2")).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackPan>(harness.trackId, -1.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackPan>(other, 1.0)).ok());

    REQUIRE(harness.router().noteOn(sim, 1, 60, 100, 0.2));
    harness.router().setTarget(other.toString(), 0.4);
    REQUIRE(harness.router().noteOff(sim, 1, 60, 0.6));
    const auto rendered = render(harness.host.edit(), 2.0);

    CHECK(levelDb(rendered.buffer, 0.3, 0.5, 0) > heardDb);
    CHECK(levelDb(rendered.buffer, 1.5, 2.0) < silentDb);
    CHECK(levelDb(rendered.buffer, 0.0, 2.0, 1) < silentDb); // the new one never sounded
}

TEST_CASE("Live: a window that loses the keyboard, a keyboard unplugged, end in silence")
{
    const EditClock clock;
    LiveHarness harness;

    SUBCASE("the window loses the computer's keyboard")
    {
        REQUIRE(harness.router().noteOn(Router::computerKeyboard, 1, 60, 100, 0.2));
        REQUIRE(harness.router().noteOn(Router::computerKeyboard, 1, 64, 100, 0.2));
        CHECK(harness.router().release(Router::computerKeyboard, 0.6) == 2);
    }
    SUBCASE("the MIDI keyboard is unplugged, pedal down")
    {
        REQUIRE(harness.router().controller(Router::firstMidiInput, 1, 64, 127, 0.1));
        REQUIRE(harness.router().noteOn(Router::firstMidiInput, 1, 67, 100, 0.2));
        REQUIRE(harness.router().noteOff(Router::firstMidiInput, 1, 67, 0.3)); // held by the pedal
        CHECK(harness.router().release(Router::firstMidiInput, 0.6) == 0);
    }

    const auto rendered = render(harness.host.edit(), 2.0);
    CHECK(levelDb(rendered.buffer, 0.25, 0.55) > heardDb);
    CHECK(levelDb(rendered.buffer, 1.5, 2.0) < silentDb);
    CHECK(harness.router().held() == 0);
}

TEST_CASE("Live: the sound card lost in the middle of a held note ends in silence")
{
    const EditClock clock;
    LiveHarness harness;
    REQUIRE(harness.router().noteOn(sim, 1, 60, 100, 0.2));

    bool lost = false;
    const auto rendered = render(harness.host.edit(),
                                 2.0,
                                 [&harness, &lost](double reached)
                                 {
                                     if (!lost && reached >= 0.6)
                                     {
                                         // What AudioOutputKeeper::onLost does.
                                         harness.router().silence(reached);
                                         lost = true;
                                     }
                                 });
    REQUIRE(lost);
    CHECK(levelDb(rendered.buffer, 0.3, 0.55) > heardDb);
    CHECK(levelDb(rendered.buffer, 1.5, 2.0) < silentDb);
    CHECK(harness.router().held() == 0);
}

TEST_CASE("Live: a note held over the end of a clip's note of the same pitch goes on sounding")
{
    // Playing over the song: the clip plays a 60 from 0 to 0.5 s, the
    // keyboard holds a 60 from 0.25 to 1.5 s. The clip's release at 0.5 s
    // must not cut the note the hand still holds.
    const EditClock clock;
    LiveHarness harness;
    const auto clip = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clip, 0.0, 4.0)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clip, NoteId::generate(), 60))
                .ok()); // a beat: 0.5 s at 120

    REQUIRE(harness.router().noteOn(sim, 1, 60, 100, 0.25));
    REQUIRE(harness.router().noteOff(sim, 1, 60, 1.5));
    const auto rendered = render(harness.host.edit(), 2.5);

    CHECK(levelDb(rendered.buffer, 0.7, 1.4) > heardDb);
    CHECK(levelDb(rendered.buffer, 2.1, 2.5) < silentDb);
}

TEST_CASE("Live: the live input is placed once, first, and a command does not make it again")
{
    LiveHarness harness;
    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);
    const auto inputs = track->pluginList.getPluginsOfType<LiveInputPlugin>();
    REQUIRE(inputs.size() == 1);
    auto* placed = inputs.getFirst();
    CHECK(track->pluginList.getPlugins().getFirst() == placed);

    // A mute and back, a volume, a note: the track's form and what it plays
    // change, the live input stays the same object.
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackMuted>(harness.trackId, true)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackMuted>(harness.trackId, false)).ok());
    REQUIRE(harness.bus.execute(harness.setVolume(-3.0)).ok());
    const auto clip = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clip)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clip, NoteId::generate())).ok());
    harness.projector.reconcile();

    const auto after = track->pluginList.getPluginsOfType<LiveInputPlugin>();
    REQUIRE(after.size() == 1);
    CHECK(after.getFirst() == placed);
    CHECK(track->pluginList.getPlugins().getFirst() == placed);
}

TEST_CASE("Live: an attack and its release at the same instant leave no voice behind")
{
    // Two messages of one note at one instant: what a burst of late messages
    // becomes once pulled to the start of a block (a stall of the card), or a
    // key tapped and released within the same sample. Tracktion sorts a
    // release before an attack at the same sample: the attack would come
    // last, and the note would never end.
    const EditClock clock;
    LiveHarness harness;
    REQUIRE(harness.router().noteOn(sim, 1, 72, 100, 0.2));
    REQUIRE(harness.router().noteOff(sim, 1, 72, 0.2));
    const auto rendered = render(harness.host.edit(), 2.0);
    CHECK(levelDb(rendered.buffer, 1.5, 2.0) < silentDb);
}
