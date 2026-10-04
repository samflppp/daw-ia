#include "EngineTestSupport.h"
#include "HostedParameters.h"
#include "TestSettings.h"
#include "daw/domain/commands/AutomationCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/engine/ClapPluginFormat.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/ParameterBridge.h"
#include "daw/engine/PluginCatalogue.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "clap/ClapPluginInstance.h"
#include <doctest/doctest.h>

using namespace daw::domain;
using daw::engine::ContentStore;
using daw::engine::PluginCatalogue;

namespace
{

// A temporary directory of its own for every test that writes: a store or a
// plugin list left behind would make the next run pass for the wrong reason.
struct TemporaryDirectory
{
    TemporaryDirectory()
    {
        directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("daw_engine_tests")
                        .getChildFile(juce::Uuid{}.toDashedString());
        directory.createDirectory();
    }

    ~TemporaryDirectory() { directory.deleteRecursively(); }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    juce::File directory;
};

// Root mean square of what actually came out of the Edit. This is the whole
// point of the audio tests: a non-null pointer proves nothing, a signal does.
double renderedRms(tracktion::Edit& edit)
{
    auto rendered = tracktion::test_utilities::renderToAudioBuffer(edit);
    if (rendered.buffer.getNumSamples() == 0)
        return 0.0;

    double sum = 0.0;
    for (int channel = 0; channel < rendered.buffer.getNumChannels(); ++channel)
    {
        const auto* samples = rendered.buffer.getReadPointer(channel);
        for (int sample = 0; sample < rendered.buffer.getNumSamples(); ++sample)
            sum += static_cast<double>(samples[sample]) * samples[sample];
    }

    const auto count =
        static_cast<double>(rendered.buffer.getNumSamples()) * rendered.buffer.getNumChannels();
    return std::sqrt(sum / count);
}

// A real third-party plugin, named by the environment rather than guessed:
//   DAW_TEST_VST3=C:\Program Files\Common Files\VST3\Kontakt 7.vst3
//   DAW_TEST_CLAP=C:\Program Files\Common Files\CLAP\Vital.clap
//
// Variable absent: the test is skipped, and the bilan says so. Variable present
// and no sound: the test fails. It is never green for the wrong reason.
juce::File pluginFromEnvironment(const char* variable)
{
    const auto path = juce::SystemStats::getEnvironmentVariable(variable, {});
    if (path.isEmpty())
        return {};

    return juce::File{path};
}

// Everything the plugin tests need: a real engine, a catalogue and a store in a
// temporary directory, the projector wired to both, and the parameter bridge.
struct PluginHarness
{
    PluginHarness()
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry}
        , host{"daw_engine_tests", daw::testing::engineSettingsFolder()}
        , catalogue{host.engine(), temporary.directory.getChildFile("plugins.xml")}
        , store{temporary.directory.getChildFile("plugin-state")}
        , projector{host.edit(), state, &catalogue, &store}
        , bridge{bus, state, host.edit(), projector}
    {
        bus.addObserver(projector);

        Track track{};
        track.id = trackId;
        track.name = "Piste plugin";
        REQUIRE(state.addTrack(track).ok());
        projector.reconcile();
    }

    // Registers one plugin file in the catalogue without scanning a whole
    // directory, and returns its description.
    [[nodiscard]] std::optional<juce::PluginDescription> registerPlugin(const juce::File& file,
                                                                        const juce::String& formatName)
    {
        auto& manager = host.engine().getPluginManager();

        for (auto* format : manager.pluginFormatManager.getFormats())
        {
            if (format == nullptr || format->getName() != formatName)
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile(found, file.getFullPathName());

            for (auto* description : found)
            {
                if (description == nullptr)
                    continue;

                manager.knownPluginList.addType(*description);
                return *description;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::unique_ptr<Command> insert(const PluginInstance& plugin, std::size_t index = 0) const
    {
        return std::make_unique<InsertPlugin>(trackId, plugin, index);
    }

    void playThreeNotes()
    {
        const auto clipId = ClipId::generate();
        REQUIRE(bus.execute(std::make_unique<CreateMidiClip>(trackId, clipId, 0.0, 2.0)).ok());

        double start = 0.0;
        for (const int pitch : {60, 64, 67})
        {
            Note note{};
            note.id = NoteId::generate();
            note.pitch = pitch;
            note.velocity = 110;
            note.startBeats = start;
            note.lengthBeats = 0.5;
            start += 0.5;

            REQUIRE(bus.execute(std::make_unique<AddNote>(clipId, note)).ok());
        }
    }

    TemporaryDirectory temporary;
    ProjectState state;
    CommandRegistry registry;
    CommandBus bus;
    daw::engine::EngineHost host;
    PluginCatalogue catalogue;
    ContentStore store;
    daw::engine::ProjectProjector projector;
    daw::engine::ParameterBridge bridge;
    TrackId trackId{TrackId::generate()};
};

} // namespace

// ---------------------------------------------------------------------------
// The store
// ---------------------------------------------------------------------------

TEST_CASE("A plugin state comes back out of the store byte for byte")
{
    TemporaryDirectory temporary;
    ContentStore store{temporary.directory};

    std::vector<std::byte> blob(64 * 1024);
    for (std::size_t index = 0; index < blob.size(); ++index)
        blob[index] = static_cast<std::byte>(index * 7 + 3);

    auto reference = store.put(blob.data(), blob.size());
    REQUIRE(reference.ok());
    CHECK(reference.value().byteCount == blob.size());
    CHECK(reference.value().digest.size() == StateBlobRef::digestLength);
    CHECK(reference.value().validate().ok());

    auto restored = store.get(reference.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().getSize() == blob.size());
    CHECK(std::memcmp(restored.value().getData(), blob.data(), blob.size()) == 0);
}

TEST_CASE("The same state stored twice is one file, and an empty state is no file")
{
    TemporaryDirectory temporary;
    ContentStore store{temporary.directory};

    const std::string blob = "the same bytes, twice";

    auto first = store.put(blob.data(), blob.size());
    auto second = store.put(blob.data(), blob.size());
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.value() == second.value());

    juce::Array<juce::File> files;
    temporary.directory.findChildFiles(files, juce::File::findFiles, true);
    CHECK(files.size() == 1);

    auto empty = store.put(nullptr, 0);
    REQUIRE(empty.ok());
    CHECK(empty.value().isEmpty());
    CHECK(store.contains(empty.value()));
}

TEST_CASE("A damaged state is refused instead of being handed to a plugin")
{
    TemporaryDirectory temporary;
    ContentStore store{temporary.directory};

    const std::string blob = "bytes that will be tampered with";
    auto reference = store.put(blob.data(), blob.size());
    REQUIRE(reference.ok());

    // Rewrite the file under its own digest: exactly what a failing disk or a
    // careless synchronisation tool does.
    juce::Array<juce::File> files;
    temporary.directory.findChildFiles(files, juce::File::findFiles, true);
    REQUIRE(files.size() == 1);
    REQUIRE(files.getFirst().replaceWithText("bytes that were tampered with!!!"));

    auto restored = store.get(reference.value());
    REQUIRE_FALSE(restored.ok());
    CHECK(restored.error().code == ErrorCode::serialisationError);
}

// ---------------------------------------------------------------------------
// The catalogue
// ---------------------------------------------------------------------------

TEST_CASE("A plugin that killed the scanner is blacklisted at the next start")
{
    TemporaryDirectory temporary;
    daw::engine::EngineHost host{"daw_engine_tests", daw::testing::engineSettingsFolder()};

    const auto listFile = temporary.directory.getChildFile("plugins.xml");
    const auto hostile = temporary.directory.getChildFile("hostile.vst3").getFullPathName();

    // What the scanner leaves behind when it dies with a plugin open.
    REQUIRE(listFile.getSiblingFile("plugins-scanning.txt").replaceWithText(hostile));

    PluginCatalogue catalogue{host.engine(), listFile};
    catalogue.load();

    CHECK(catalogue.blacklist().contains(hostile));
}

TEST_CASE("The plugin list is persisted, so a start costs no scan")
{
    TemporaryDirectory temporary;
    daw::engine::EngineHost host{"daw_engine_tests", daw::testing::engineSettingsFolder()};
    const auto listFile = temporary.directory.getChildFile("plugins.xml");

    juce::PluginDescription description;
    description.name = "Faux synthe";
    description.pluginFormatName = daw::engine::ClapPluginFormat::formatName;
    description.fileOrIdentifier =
        temporary.directory.getChildFile("faux.clap").getFullPathName() + "|com.test.faux";
    description.isInstrument = true;
    description.uniqueId = 4242;

    PluginCatalogue catalogue{host.engine(), listFile};
    REQUIRE(host.engine().getPluginManager().knownPluginList.addType(description));
    REQUIRE(catalogue.save().ok());
    REQUIRE(listFile.existsAsFile());

    // A second catalogue over the same file, and a plugin list emptied in
    // between: what a restart looks like.
    host.engine().getPluginManager().knownPluginList.clear();
    PluginCatalogue reloaded{host.engine(), listFile};
    reloaded.load();

    const auto found = reloaded.find(PluginCatalogue::refFor(description));
    REQUIRE(found.has_value());
    CHECK(found->name == description.name);
}

TEST_CASE("A CLAP identity is the plugin id, not a path")
{
    juce::PluginDescription description;
    description.name = "Faux synthe";
    description.pluginFormatName = daw::engine::ClapPluginFormat::formatName;
    description.fileOrIdentifier = "/somewhere/else/faux.clap|com.test.faux";

    const auto ref = PluginCatalogue::refFor(description);
    CHECK(ref.format == PluginRef::clapFormat);
    CHECK(ref.identifier == "com.test.faux");
    CHECK(ref.validate().ok());
}

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------

TEST_CASE("A plugin this machine does not have is named, not replaced")
{
    PluginHarness harness;

    PluginInstance missing{};
    missing.id = PluginId::generate();
    missing.ref.format = std::string{PluginRef::vst3Format};
    missing.ref.identifier = "un-plugin-qui-n-existe-pas";
    missing.ref.name = "Absent";

    REQUIRE(harness.bus.execute(harness.insert(missing)).ok());

    // The command succeeded: the project keeps the instance, so reinstalling the
    // plugin is enough to get the sound back.
    CHECK(harness.state.findPlugin(missing.id) != nullptr);
    CHECK(harness.projector.missingPlugins().size() == 1);
    CHECK(harness.projector.missingPlugins().front().find("un-plugin-qui-n-existe-pas") != std::string::npos);
}

TEST_CASE("Three notes on the fallback synth come out as a signal, not as a pointer")
{
    PluginHarness harness;
    harness.playThreeNotes();

    // The S3 lesson, applied: what is measured is what left the Edit.
    CHECK(renderedRms(harness.host.edit()) > 0.001);
}

TEST_CASE("The parameter bridge reports no lost movement when nothing moves")
{
    PluginHarness harness;
    harness.playThreeNotes();
    CHECK(harness.bridge.droppedMovements() == 0);
}

// ---------------------------------------------------------------------------
// Real plugins. Skipped without the environment variable, red with it and no
// sound.
// ---------------------------------------------------------------------------

namespace
{

void hostRealPlugin(const char* variable, const juce::String& formatName)
{
    const auto file = pluginFromEnvironment(variable);
    if (file == juce::File{})
    {
        MESSAGE("skipped: " << variable << " is not set, so no real " << formatName.toStdString()
                            << " plugin was hosted");
        return;
    }

    REQUIRE_MESSAGE((file.existsAsFile() || file.isDirectory()),
                    "the plugin named by " << variable << " does not exist");

    PluginHarness harness;

    const auto description = harness.registerPlugin(file, formatName);
    REQUIRE_MESSAGE(description.has_value(), "the plugin could not be scanned: " << file.getFullPathName());

    PluginInstance instance{};
    instance.id = PluginId::generate();
    instance.ref = PluginCatalogue::refFor(*description);

    REQUIRE(harness.bus.execute(harness.insert(instance)).ok());
    CHECK(harness.projector.missingPlugins().empty());

    // The plugin is really in the chain, and it is the one that was asked for.
    auto* track = tracktion::getAudioTracks(harness.host.edit()).getFirst();
    REQUIRE(track != nullptr);

    tracktion::ExternalPlugin* hosted = nullptr;
    for (auto plugin : track->pluginList.getPlugins())
    {
        if (auto* external = dynamic_cast<tracktion::ExternalPlugin*>(plugin); external != nullptr)
            hosted = external;
    }
    REQUIRE_MESSAGE(hosted != nullptr, "no external plugin in the chain");
    REQUIRE_MESSAGE(hosted->getLoadError().isEmpty(), hosted->getLoadError().toStdString());

    harness.playThreeNotes();

    // The proof of the week: a real third-party instrument, notes sent through
    // the bus, and a signal measured at the output.
    CHECK(renderedRms(harness.host.edit()) > 0.0001);

    // And its opaque state makes the round trip through the store.
    if (auto* instrument = hosted->getAudioPluginInstance(); instrument != nullptr)
    {
        juce::MemoryBlock blob;
        instrument->getStateInformation(blob);

        if (blob.getSize() > 0)
        {
            auto reference = harness.store.put(blob.getData(), blob.getSize());
            REQUIRE(reference.ok());
            REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(instance.id, reference.value()))
                        .ok());

            const auto* stored = harness.state.findPlugin(instance.id);
            REQUIRE(stored != nullptr);
            CHECK(stored->state.digest == reference.value().digest);
            CHECK(harness.store.contains(stored->state));

            auto restored = harness.store.get(stored->state);
            REQUIRE(restored.ok());
            CHECK(restored.value().getSize() == blob.getSize());
        }
    }
}

} // namespace

TEST_CASE("A real VST3 instrument loads, plays and yields its state")
{
    hostRealPlugin("DAW_TEST_VST3", "VST3");
}

TEST_CASE("A real CLAP instrument loads, plays and yields its state")
{
    hostRealPlugin("DAW_TEST_CLAP", daw::engine::ClapPluginFormat::formatName);
}

// ---------------------------------------------------------------------------
// The CLAP host, proved against a real CLAP binary built by this repository.
// ---------------------------------------------------------------------------

namespace
{

juce::File clapFixture()
{
    return juce::File{juce::String{DAW_TEST_CLAP_FIXTURE}};
}

tracktion::ExternalPlugin* hostedPluginIn(tracktion::Edit& edit)
{
    auto* track = tracktion::getAudioTracks(edit).getFirst();
    if (track == nullptr)
        return nullptr;

    for (auto plugin : track->pluginList.getPlugins())
    {
        if (auto* external = dynamic_cast<tracktion::ExternalPlugin*>(plugin); external != nullptr)
            return external;
    }
    return nullptr;
}

} // namespace

TEST_CASE("A CLAP plugin is scanned, and its identity is its own plugin id")
{
    PluginHarness harness;

    const auto description = harness.registerPlugin(clapFixture(), daw::engine::ClapPluginFormat::formatName);
    REQUIRE(description.has_value());

    CHECK(description->pluginFormatName == daw::engine::ClapPluginFormat::formatName);
    CHECK(description->name == "DAW Test Sine");
    CHECK(description->isInstrument);
    CHECK(description->numOutputChannels == 2);

    const auto ref = PluginCatalogue::refFor(*description);
    CHECK(ref.format == PluginRef::clapFormat);
    CHECK(ref.identifier == "daw.test.sine");

    // And the catalogue finds it back from the reference a project would store.
    const auto found = harness.catalogue.find(ref);
    REQUIRE(found.has_value());
    CHECK(found->fileOrIdentifier == description->fileOrIdentifier);
}

TEST_CASE("A CLAP instrument on a track turns notes into a signal")
{
    PluginHarness harness;

    const auto description = harness.registerPlugin(clapFixture(), daw::engine::ClapPluginFormat::formatName);
    REQUIRE(description.has_value());

    PluginInstance instance{};
    instance.id = PluginId::generate();
    instance.ref = PluginCatalogue::refFor(*description);

    REQUIRE(harness.bus.execute(harness.insert(instance)).ok());
    CHECK(harness.projector.missingPlugins().empty());

    auto* hosted = hostedPluginIn(harness.host.edit());
    REQUIRE(hosted != nullptr);
    CHECK(hosted->getLoadError().isEmpty());

    // The synth of the project replaced the fallback: two instruments would
    // play the same notes at once.
    auto* track = tracktion::getAudioTracks(harness.host.edit()).getFirst();
    REQUIRE(track != nullptr);
    CHECK(track->pluginList.getPluginsOfType<tracktion::FourOscPlugin>().isEmpty());

    harness.playThreeNotes();

    // What is measured is the signal, never a pointer.
    CHECK(renderedRms(harness.host.edit()) > 0.01);
}

TEST_CASE("A parameter command reaches the CLAP plugin, and is heard")
{
    PluginHarness harness;

    const auto description = harness.registerPlugin(clapFixture(), daw::engine::ClapPluginFormat::formatName);
    REQUIRE(description.has_value());

    PluginInstance instance{};
    instance.id = PluginId::generate();
    instance.ref = PluginCatalogue::refFor(*description);
    REQUIRE(harness.bus.execute(harness.insert(instance)).ok());

    harness.playThreeNotes();
    const auto atDefaultGain = renderedRms(harness.host.edit());
    REQUIRE(atDefaultGain > 0.01);

    // The parameter identity is the CLAP parameter id, the one the journal holds.
    auto* hosted = hostedPluginIn(harness.host.edit());
    REQUIRE(hosted != nullptr);
    // The plugin's own parameters, not Tracktion's dry and wet levels.
    const auto hostedParams = daw::engine::hostedParameters(*hosted);
    REQUIRE(hostedParams.size() == 1);
    const auto paramId = hostedParams.front().first;
    CHECK(paramId == "0"); // the CLAP parameter id of the fixture's gain

    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(instance.id, paramId, 0.0)).ok());
    CHECK(renderedRms(harness.host.edit()) < atDefaultGain / 10.0);

    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(instance.id, paramId, 1.0)).ok());
    CHECK(renderedRms(harness.host.edit()) > atDefaultGain * 1.5);
}

namespace
{

// The first and the second half of a render, each as one RMS.
std::pair<double, double> renderedHalves(tracktion::Edit& edit)
{
    auto rendered = tracktion::test_utilities::renderToAudioBuffer(edit);
    const auto half = rendered.buffer.getNumSamples() / 2;
    if (half == 0)
        return {0.0, 0.0};

    const auto rms = [&rendered](int start, int length)
    {
        double sum = 0.0;
        for (int channel = 0; channel < rendered.buffer.getNumChannels(); ++channel)
        {
            const auto* samples = rendered.buffer.getReadPointer(channel, start);
            for (int index = 0; index < length; ++index)
                sum += static_cast<double>(samples[index]) * samples[index];
        }
        return std::sqrt(sum / (static_cast<double>(length) * rendered.buffer.getNumChannels()));
    };
    return {rms(0, half), rms(half, half)};
}

} // namespace

TEST_CASE("An automated plugin parameter is heard moving, and survives its plugin going and coming back")
{
    PluginHarness harness;

    const auto description = harness.registerPlugin(clapFixture(), daw::engine::ClapPluginFormat::formatName);
    REQUIRE(description.has_value());

    PluginInstance instance{};
    instance.id = PluginId::generate();
    instance.ref = PluginCatalogue::refFor(*description);
    REQUIRE(harness.bus.execute(harness.insert(instance)).ok());
    harness.playThreeNotes();

    // Each half against itself without automation: the three notes do not
    // fill the two halves alike, the gain is what has to differ.
    const auto [steadyFirst, steadySecond] = renderedHalves(harness.host.edit());
    REQUIRE(steadyFirst > 0.01);
    REQUIRE(steadySecond > 0.01);

    // The gain of the fixture, named by its CLAP id, rising from nothing.
    std::vector<AutomationPoint> points(2);
    points[0].id = AutomationPointId::generate();
    points[0].beats = 0.0;
    points[0].value = 0.0;
    points[1].id = AutomationPointId::generate();
    points[1].beats = 2.0;
    points[1].value = 1.0;
    const auto target = AutomationTarget::parameterOf(instance.id, "0");
    REQUIRE(harness.bus
                .execute(
                    std::make_unique<WriteAutomation>(AutomationLineId::generate(), target, 0.0, 2.0, points))
                .ok());
    const auto depth = harness.bus.undoDepth();

    const auto [quiet, loud] = renderedHalves(harness.host.edit());
    MESSAGE("gain automated 0 -> 1: first half " << quiet / steadyFirst << " of itself, second half "
                                                 << loud / steadySecond);
    CHECK(loud / steadySecond > 2.0 * quiet / steadyFirst);

    // The parameter moved during the render and nobody touched it: the
    // bridge must not have turned the automation into commands.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    CHECK(harness.bus.undoDepth() == depth);

    // The plugin goes, and its line with it; the Edit still renders.
    REQUIRE(harness.bus.execute(std::make_unique<RemovePlugin>(instance.id)).ok());
    CHECK(harness.state.findAutomationLineFor(target) == nullptr);
    static_cast<void>(renderedHalves(harness.host.edit()));

    // And comes back with it, heard rising again.
    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.state.findAutomationLineFor(target) != nullptr);
    const auto [quietAgain, loudAgain] = renderedHalves(harness.host.edit());
    MESSAGE("after the undo: first half " << quietAgain / steadyFirst << " of itself, second half "
                                          << loudAgain / steadySecond);
    CHECK(loudAgain / steadySecond > 2.0 * quietAgain / steadyFirst);
}

TEST_CASE("A captured CLAP state, put on a new instance, brings its sound back")
{
    PluginHarness harness;

    const auto description = harness.registerPlugin(clapFixture(), daw::engine::ClapPluginFormat::formatName);
    REQUIRE(description.has_value());

    const auto ref = PluginCatalogue::refFor(*description);

    // First instance, nothing captured: the plugin sounds at its own default.
    PluginInstance plain{};
    plain.id = PluginId::generate();
    plain.ref = ref;
    REQUIRE(harness.bus.execute(harness.insert(plain)).ok());

    harness.playThreeNotes();
    const auto atDefaultGain = renderedRms(harness.host.edit());
    REQUIRE(atDefaultGain > 0.01);

    auto* hosted = hostedPluginIn(harness.host.edit());
    REQUIRE(hosted != nullptr);
    const auto hostedParams = daw::engine::hostedParameters(*hosted);
    REQUIRE(hostedParams.size() == 1);
    const auto paramId = hostedParams.front().first;

    // Turn the gain up, then capture the opaque state the plugin itself writes.
    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(plain.id, paramId, 1.0)).ok());
    const auto loud = renderedRms(harness.host.edit());
    REQUIRE(loud > atDefaultGain * 1.5);

    juce::MemoryBlock blob;
    REQUIRE(hosted->getAudioPluginInstance() != nullptr);
    hosted->getAudioPluginInstance()->getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    auto reference = harness.store.put(blob.getData(), blob.getSize());
    REQUIRE(reference.ok());
    REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(plain.id, reference.value())).ok());
    CHECK(harness.state.findPlugin(plain.id)->state == reference.value());

    // A second instance, with no parameter of its own, only the captured state.
    // If the blob is applied it sounds like the loud one; if it were ignored it
    // would fall back to the plugin default. That is the whole design of the
    // week, measured in samples.
    REQUIRE(harness.bus.execute(std::make_unique<RemovePlugin>(plain.id)).ok());

    PluginInstance restored{};
    restored.id = PluginId::generate();
    restored.ref = ref;
    restored.state = reference.value();
    REQUIRE(harness.bus.execute(harness.insert(restored)).ok());

    const auto afterRestore = renderedRms(harness.host.edit());
    CHECK(afterRestore > loud * 0.9);
    CHECK(afterRestore > atDefaultGain * 1.5);
}

TEST_CASE("A knob turned inside the plugin becomes one undoable history entry")
{
    PluginHarness harness;

    const auto description = harness.registerPlugin(clapFixture(), daw::engine::ClapPluginFormat::formatName);
    REQUIRE(description.has_value());

    PluginInstance instance{};
    instance.id = PluginId::generate();
    instance.ref = PluginCatalogue::refFor(*description);
    REQUIRE(harness.bus.execute(harness.insert(instance)).ok());

    const auto depthBefore = harness.bus.undoDepth();

    auto* hosted = hostedPluginIn(harness.host.edit());
    REQUIRE(hosted != nullptr);
    const auto hostedParams = daw::engine::hostedParameters(*hosted);
    REQUIRE(hostedParams.size() == 1);
    auto* parameter = hostedParams.front().second;
    REQUIRE(parameter != nullptr);

    // What the window of a plugin does: a gesture, a sweep, the end of the
    // gesture. The bridge is the only thing between that and the bus.
    parameter->parameterChangeGestureBegin();
    for (int frame = 0; frame < 20; ++frame)
        parameter->setNormalisedParameter(0.05f * static_cast<float>(frame), juce::sendNotification);
    parameter->parameterChangeGestureEnd();

    // The bridge reports on the message thread, so the loop has to run.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);

    CHECK(harness.bridge.droppedMovements() == 0);
    CHECK(harness.bus.undoDepth() == depthBefore + 1);

    const auto* plugin = harness.state.findPlugin(instance.id);
    REQUIRE(plugin != nullptr);
    const auto paramId = hostedParams.front().first;
    REQUIRE(plugin->findParam(paramId) != nullptr);
    CHECK(plugin->findParam(paramId)->value == doctest::Approx(0.95).epsilon(0.02));

    // One undo, and the whole sweep is gone: the parameter goes back to the
    // state of the plugin instead of a default written into the project.
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findPlugin(instance.id)->params.empty());
}

// ---------------------------------------------------------------------------
// The CLAP host on its own, with no Tracktion around it: the shortest path
// between a parameter and a sample.
// ---------------------------------------------------------------------------

TEST_CASE("The CLAP host sends a parameter to the plugin, and the samples follow")
{
    juce::String error;
    auto instance =
        daw::engine::clap_host::PluginInstance::create(clapFixture(), "daw.test.sine", 44100.0, 512, error);
    REQUIRE_MESSAGE(instance != nullptr, error.toStdString());

    REQUIRE(instance->getParameters().size() == 1);
    auto* gain = instance->getParameters()[0];
    REQUIRE(gain != nullptr);

    instance->prepareToPlay(44100.0, 512);

    const auto rmsOf = [&instance](float normalisedGain)
    {
        instance->getParameters()[0]->setValue(normalisedGain);

        juce::AudioBuffer<float> buffer{2, 512};
        buffer.clear();

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);

        double sum = 0.0;
        for (int block = 0; block < 4; ++block)
        {
            instance->processBlock(buffer, midi);
            midi.clear();

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                sum += static_cast<double>(buffer.getSample(0, sample)) * buffer.getSample(0, sample);
        }

        return std::sqrt(sum / (4.0 * 512.0));
    };

    const auto atDefault = rmsOf(0.5f);
    MESSAGE("rms at 0.5 = " << atDefault);
    CHECK(atDefault > 0.1);

    const auto silent = rmsOf(0.0f);
    MESSAGE("rms at 0.0 = " << silent);
    CHECK(silent < 0.01);

    const auto loud = rmsOf(1.0f);
    MESSAGE("rms at 1.0 = " << loud);
    CHECK(loud > atDefault * 1.5);
}
