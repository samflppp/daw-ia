#include "EngineTestSupport.h"
#include "TestSettings.h"
#include "daw/domain/buses/Shared.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/MixRender.h"
#include "daw/engine/PluginCatalogue.h"
#include "daw/engine/ProjectProjector.h"

#include <atomic>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::engine::MixRender;

// The smart buses by a send (S24 §7.3, decided on 7 October 2026): six tracks
// carrying the same reverb, proposed on one bus, rendered before and after
// with a real reverb. DAW IA has none of its own, and a check never depends on
// the person's plugins: the reverb is named by DAW_TEST_VST3, and the test is
// skipped without it, as every real-plugin test.
//
//   DAW_TEST_VST3=C:\Program Files\Common Files\VST3\ValhallaDSP\ValhallaSupermassive.vst3
//
// Each track plays one short note on its instrument, then 3.5 s of nothing:
// what is heard after is the reverb's tail alone. The person's plugins only
// process the instrument, not the audio clips (the companion track, an open
// debt since S20), so the notes are MIDI. The fallback synth's phase changes
// at each render (S24): the tails are compared by level, not sample by sample.
//
// What it proves: before, six reverbs make a tail; after, the bus carries
// one, and its tail has the same level. What it measures and says: the dry
// sound the reverb lets through, which each send now adds to the track's own.

namespace
{

constexpr int tracks = 6;
constexpr double clipBeats = 8.0; // 4 s at 120 BPM: the length of the render
constexpr double noteBeats = 0.5; // 0.25 s
constexpr double tailFrom = 1.5;  // seconds: the notes and their release over
constexpr double tailTo = 3.75;

juce::AudioBuffer<float> read(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples)};
    REQUIRE(reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true));
    return buffer;
}

// The level over [from, to) seconds, dBFS.
double levelDb(const juce::AudioBuffer<float>& buffer, double rate, double from, double to)
{
    const auto first = static_cast<int>(from * rate);
    const auto last = std::min(static_cast<int>(to * rate), buffer.getNumSamples());
    double sum = 0.0;
    int count = 0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int index = first; index < last; ++index)
        {
            const auto value = static_cast<double>(buffer.getSample(channel, index));
            sum += value * value;
            ++count;
        }
    return count > 0 && sum > 0.0 ? 10.0 * std::log10(sum / count) : -240.0;
}

juce::AudioBuffer<float> render(daw::engine::EngineHost& host,
                                const ProjectState& state,
                                const ProjectState* proposed,
                                daw::engine::PluginCatalogue& catalogue,
                                daw::engine::ContentStore& store)
{
    auto prepared = MixRender::prepare(host.edit(), state, proposed, &catalogue, &store);
    REQUIRE(prepared != nullptr);
    std::unique_ptr<MixRender::Measured> measured;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    std::thread worker{[&]
                       {
                           measured = prepared->run(cancelled, {});
                           done = true;
                       }};
    const auto until = juce::Time::getMillisecondCounterHiRes() + 300000.0;
    while (!done && juce::Time::getMillisecondCounterHiRes() < until)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    if (!done)
        cancelled = true;
    worker.join();
    REQUIRE(measured != nullptr);
    const auto file = prepared->releaseFile();
    auto buffer = read(file);
    static_cast<void>(file.deleteFile());
    return buffer;
}

} // namespace

TEST_CASE("Six tracks carrying the same reverb: one on a bus by sends, its tail heard at the same level")
{
    const auto path = juce::SystemStats::getEnvironmentVariable("DAW_TEST_VST3", {});
    if (path.isEmpty())
    {
        MESSAGE("skipped: DAW_TEST_VST3 is not set, so no reverb was shared on a bus");
        return;
    }
    const juce::File file{path};
    REQUIRE_MESSAGE((file.existsAsFile() || file.isDirectory()),
                    "the plugin named by DAW_TEST_VST3 does not exist");

    ProjectState state;
    auto registry = CommandRegistry::withBuiltinCommands();
    CommandBus bus{state, registry};
    daw::engine::EngineHost host{"daw_engine_tests", daw::testing::engineSettingsFolder()};
    const auto folder = juce::File::createTempFile("smartbus");
    daw::engine::ContentStore store{folder.getChildFile("blobs")};
    daw::engine::PluginCatalogue catalogue{host.engine(), folder.getChildFile("plugins.xml")};
    daw::engine::ProjectProjector projector{host.edit(), state, &catalogue, &store};
    bus.addObserver(projector);
    const auto rate = host.engine().getDeviceManager().getSampleRate();

    // The reverb, registered without scanning a directory.
    std::optional<juce::PluginDescription> description;
    auto& manager = host.engine().getPluginManager();
    for (auto* format : manager.pluginFormatManager.getFormats())
        if (format != nullptr && format->getName() == "VST3" && !description)
        {
            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile(found, file.getFullPathName());
            if (!found.isEmpty() && found[0] != nullptr)
            {
                manager.knownPluginList.addType(*found[0]);
                description = *found[0];
            }
        }
    REQUIRE_MESSAGE(description.has_value(), "the plugin could not be scanned: " << path.toStdString());
    MESSAGE("plugin: " << description->name.toStdString() << ", category « "
                       << description->category.toStdString() << " »");
    // The person names a reverb; the catalogue may file it only as « Fx »
    // (Valhalla does), and then the page « Bus » does not propose it: said.
    if (!description->category.toLowerCase().contains("reverb"))
        MESSAGE("the catalogue does not file it as a reverb: the page « Bus » would not propose it");

    for (int index = 0; index < tracks; ++index)
    {
        const auto track = TrackId::generate();
        REQUIRE(
            bus.execute(std::make_unique<AddTrack>(track, "Piste " + std::to_string(index + 1), 0.0)).ok());
        const auto clip = ClipId::generate();
        REQUIRE(bus.execute(std::make_unique<CreateMidiClip>(track, clip, 0.0, clipBeats)).ok());
        Note note{};
        note.id = NoteId::generate();
        note.pitch = 60 + 2 * index;
        note.velocity = 100;
        note.startBeats = 0.0;
        note.lengthBeats = noteBeats;
        REQUIRE(bus.execute(std::make_unique<AddNote>(clip, note)).ok());
        PluginInstance reverb{};
        reverb.id = PluginId::generate();
        reverb.ref = daw::engine::PluginCatalogue::refFor(*description);
        REQUIRE(bus.execute(std::make_unique<InsertPlugin>(track, reverb, 0)).ok());
    }
    REQUIRE(projector.missingPlugins().empty());

    // The proposal, the plugin named taken for the reverb it is.
    const auto proposals = buses::propose(
        state,
        [](const PluginRef&)
        { return buses::Recognition{buses::Kind::reverb, buses::KnownBy::catalogue, std::nullopt}; });
    REQUIRE(proposals.size() == 1);
    const auto& shared = proposals.front();
    CHECK(shared.way == buses::Way::send);
    CHECK(shared.tracks.size() == static_cast<std::size_t>(tracks));
    MESSAGE(shared.sentence);

    auto proposed = state;
    for (const auto& command :
         buses::compile(shared, TrackId::generate(), "Bus réverbération", PluginId::generate()))
        REQUIRE(command->apply(proposed).ok());

    // The dry: the same tracks, no reverb anywhere.
    auto dry = state;
    for (const auto& track : state.tracks())
        for (const auto& plugin : track.plugins)
            REQUIRE(dry.removePlugin(plugin.id).ok());

    const auto before = render(host, state, nullptr, catalogue, store);
    const auto after = render(host, state, &proposed, catalogue, store);
    const auto drySound = render(host, state, &dry, catalogue, store);

    const auto tailDry = levelDb(drySound, rate, tailFrom, tailTo);
    const auto tailBefore = levelDb(before, rate, tailFrom, tailTo);
    const auto tailAfter = levelDb(after, rate, tailFrom, tailTo);
    MESSAGE("queue : sans réverbération " << tailDry << " dBFS, avant " << tailBefore << " dBFS, après "
                                          << tailAfter << " dBFS");
    CHECK(tailDry < -90.0);                        // the notes are over
    CHECK(tailBefore > tailDry + 30.0);            // six reverbs heard
    CHECK(tailAfter > tailDry + 30.0);             // the bus's reverb heard
    CHECK(std::abs(tailAfter - tailBefore) < 3.0); // at the same level

    // The dry the reverb lets through, added by each send to the track's own.
    const auto notesDry = levelDb(drySound, rate, 0.0, 0.5);
    const auto notesBefore = levelDb(before, rate, 0.0, 0.5);
    const auto notesAfter = levelDb(after, rate, 0.0, 0.5);
    MESSAGE("pendant les notes : sans réverbération " << notesDry << " dBFS, avant " << notesBefore
                                                      << " dBFS, après " << notesAfter << " dBFS");

    static_cast<void>(folder.deleteRecursively());
}
