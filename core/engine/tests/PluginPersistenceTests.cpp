#include "HostedParameters.h"
#include "PluginPersistenceScenario.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ClapPluginFormat.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/EngineHost.h"
#include "daw/engine/PluginCatalogue.h"
#include "daw/engine/ProjectProjector.h"
#include "daw/persistence/ProjectStore.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <cmath>
#include <memory>
#include <optional>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::engine::ContentStore;
using daw::engine::PluginCatalogue;
using daw::persistence::ProjectFolder;
using daw::persistence::ProjectStore;

namespace
{

// Fixed identifiers: the process that writes and the process that reads have
// to name the same track and the same plugin instance.
constexpr const char* trackIdText = "01JBWQ7Z0000000000000TRACK";
constexpr const char* clipIdText = "01JBWQ7Z0000000000000CL1P0";
constexpr const char* pluginIdText = "01JBWQ7Z00000000000PLAG1N0";

// The blobs of a project live inside the project folder, and the layout is
// named in one place: core/persistence.
juce::File blobsFolderOf(const juce::File& projectFolder)
{
    const ProjectFolder folder{projectFolder.getFullPathName().toStdString()};
    return juce::File{juce::String{folder.blobsFolder().string()}};
}

juce::File clapFixture()
{
    return juce::File{juce::String{DAW_TEST_CLAP_FIXTURE}};
}

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

// One project, one engine, one bus: what both processes build, each in its own
// process, around the same folder on disk.
struct HostedProject
{
    explicit HostedProject(const juce::File& projectFolder)
        : registry{CommandRegistry::withBuiltinCommands()}
        , bus{state, registry}
        , host{"daw_engine_tests"}
        , catalogue{host.engine(), projectFolder.getChildFile("plugins.xml")}
        , store{blobsFolderOf(projectFolder)}
        , projector{host.edit(), state, &catalogue, &store}
    {
        bus.addObserver(projector);
    }

    // The plugin binary is machine state, not project state: the second
    // process finds it again by scanning, and the project only ever held the
    // PluginRef. That is the whole reason a project moves between machines.
    [[nodiscard]] std::optional<juce::PluginDescription> registerFixture()
    {
        auto& manager = host.engine().getPluginManager();
        for (auto* format : manager.pluginFormatManager.getFormats())
        {
            if (format == nullptr || format->getName() != daw::engine::ClapPluginFormat::formatName)
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile(found, clapFixture().getFullPathName());
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

    [[nodiscard]] tracktion::ExternalPlugin* hostedPlugin()
    {
        // On the strip or on the track of the notes (S21).
        for (auto* track : tracktion::getAudioTracks(host.edit()))
        {
            for (auto plugin : track->pluginList.getPlugins())
            {
                if (auto* external = dynamic_cast<tracktion::ExternalPlugin*>(plugin); external != nullptr)
                    return external;
            }
        }
        return nullptr;
    }

    ProjectState state;
    CommandRegistry registry;
    CommandBus bus;
    daw::engine::EngineHost host;
    PluginCatalogue catalogue;
    ContentStore store;
    daw::engine::ProjectProjector projector;
};

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

int runChild(const juce::File& projectFolder, const juce::File& rmsFile)
{
    auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    juce::StringArray arguments;
    arguments.add(executable.getFullPathName());
    arguments.add("--child");
    arguments.add("write-plugin-project");
    arguments.add(projectFolder.getFullPathName());
    arguments.add(rmsFile.getFullPathName());

    juce::ChildProcess child;
    if (!child.start(arguments))
        return -1;

    if (!child.waitForProcessToFinish(120000))
        return -2;

    return child.getExitCode();
}

} // namespace

namespace daw::testing
{
int writePluginProject(const juce::File& projectFolder, const juce::File& rmsFile)
{
    // The project outlives the store on purpose: the store detaches itself from
    // the bus when it dies, and a store that died after its bus would detach
    // from a dangling one.
    HostedProject project{projectFolder};

    auto store = ProjectStore::open(ProjectFolder{projectFolder.getFullPathName().toStdString()});
    if (!store)
        return 2;

    store.value()->startRecording(project.bus);

    const auto trackId = TrackId::parse(trackIdText).value();
    const auto clipId = ClipId::parse(clipIdText).value();
    const auto pluginId = PluginId::parse(pluginIdText).value();

    if (!project.bus.execute(std::make_unique<AddTrack>(trackId, "Synthe", 0.0)))
        return 3;

    const auto description = project.registerFixture();
    if (!description.has_value())
        return 4;

    PluginInstance instance{};
    instance.id = pluginId;
    instance.ref = PluginCatalogue::refFor(*description);

    // The generative engine is the one asking here, and the journal will say so
    // without giving it a single privilege the user does not have.
    Provenance generator{};
    generator.actor = Actor::generator;

    if (!project.bus.execute(std::make_unique<InsertPlugin>(trackId, instance, 0),
                             ExecuteOptions{{}, generator}))
        return 5;

    auto* hosted = project.hostedPlugin();
    if (hosted == nullptr || hosted->getAudioPluginInstance() == nullptr)
        return 6;

    const auto hostedParams = daw::engine::hostedParameters(*hosted);
    if (hostedParams.size() != 1)
        return 7;

    // Gain to the top, then capture the opaque state the plugin itself writes.
    if (!project.bus.execute(std::make_unique<SetPluginParameter>(pluginId, hostedParams.front().first, 1.0)))
        return 8;

    juce::MemoryBlock blob;
    hosted->getAudioPluginInstance()->getStateInformation(blob);
    if (blob.getSize() == 0)
        return 9;

    auto reference = project.store.put(blob.getData(), blob.getSize());
    if (!reference)
        return 10;

    if (!project.bus.execute(std::make_unique<CapturePluginState>(pluginId, reference.value())))
        return 11;

    if (!project.bus.execute(std::make_unique<CreateMidiClip>(trackId, clipId, 0.0, 2.0)))
        return 12;

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

        if (!project.bus.execute(std::make_unique<AddNote>(clipId, note)))
            return 13;
    }

    const auto rms = renderedRms(project.host.edit());
    if (!(rms > 0.01))
        return 14;

    if (!rmsFile.replaceWithText(juce::String(rms, 9)))
        return 15;

    store.value()->stopRecording();
    if (!store.value()->close())
        return 16;

    return 0;
}

} // namespace daw::testing

TEST_CASE("A project with a plugin, written by another process, sounds the same when reopened")
{
    TemporaryDirectory temporary;
    const auto projectFolder = temporary.directory.getChildFile("Projet.dawproj");
    const auto rmsFile = temporary.directory.getChildFile("rms.txt");

    const auto childStatus = runChild(projectFolder, rmsFile);
    REQUIRE_MESSAGE(childStatus == 0, "the writing process failed with code " << childStatus);

    const auto expectedRms = rmsFile.loadFileAsString().getDoubleValue();
    REQUIRE(expectedRms > 0.01);

    // Nothing of the first process is left: new engine, new Edit, new bus.
    HostedProject reopened{projectFolder};
    REQUIRE(reopened.registerFixture().has_value());

    auto store = ProjectStore::open(ProjectFolder{projectFolder.getFullPathName().toStdString()});
    REQUIRE(store.ok());

    auto report = store.value()->replayInto(reopened.bus);
    REQUIRE_MESSAGE(report.ok(), report.error().message);
    CHECK(report.value().commands > 0);

    const auto pluginId = PluginId::parse(pluginIdText);
    REQUIRE(pluginId.ok());
    const auto* plugin = reopened.state.findPlugin(pluginId.value());
    REQUIRE(plugin != nullptr);

    // Blob first, sparse parameters over it: both came back, and the store has
    // the bytes the digest names.
    CHECK(!plugin->state.isEmpty());
    CHECK(reopened.store.contains(plugin->state));
    REQUIRE(plugin->params.size() == 1);
    CHECK(plugin->params.front().value == doctest::Approx(1.0));

    CHECK(reopened.projector.missingPlugins().empty());

    // The measurement, in samples: the reopened project makes the same sound.
    const auto rms = renderedRms(reopened.host.edit());
    MESSAGE("rms written = " << expectedRms << ", rms reopened = " << rms);
    CHECK(rms > expectedRms * 0.9);
    CHECK(rms < expectedRms * 1.1);

    // And the history is still a history: undoing the capture and the
    // parameter takes the sound back down.
    REQUIRE(reopened.bus.canUndo());
    while (reopened.bus.undoDepth() > 2)
        REQUIRE(reopened.bus.undo().ok());

    CHECK(renderedRms(reopened.host.edit()) < rms);
}

TEST_CASE("The blobs of a project live in the project, so the folder copies whole")
{
    TemporaryDirectory temporary;
    const auto projectFolder = temporary.directory.getChildFile("Projet.dawproj");
    const auto rmsFile = temporary.directory.getChildFile("rms.txt");

    REQUIRE(runChild(projectFolder, rmsFile) == 0);

    const auto blobs = blobsFolderOf(projectFolder);
    REQUIRE(blobs.isDirectory());

    juce::Array<juce::File> stored;
    blobs.findChildFiles(stored, juce::File::findFiles, true);
    CHECK(stored.size() >= 1);

    // Copied whole, it opens somewhere else and keeps its plugin state.
    const auto copy = temporary.directory.getChildFile("Copie.dawproj");
    REQUIRE(projectFolder.copyDirectoryTo(copy));

    HostedProject reopened{copy};
    REQUIRE(reopened.registerFixture().has_value());

    auto store = ProjectStore::open(ProjectFolder{copy.getFullPathName().toStdString()});
    REQUIRE(store.ok());
    REQUIRE(store.value()->replayInto(reopened.bus).ok());

    const auto pluginId = PluginId::parse(pluginIdText);
    REQUIRE(pluginId.ok());
    const auto* plugin = reopened.state.findPlugin(pluginId.value());
    REQUIRE(plugin != nullptr);
    CHECK(reopened.store.contains(plugin->state));
    CHECK(renderedRms(reopened.host.edit()) > 0.01);
}
