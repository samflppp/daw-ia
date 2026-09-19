#include "MainWindow.h"
#include "PluginWindow.h"
#include "daw/domain/BuildInfo.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/EngineHost.h"
#include "daw/engine/ParameterBridge.h"
#include "daw/engine/ProjectProjector.h"
#include "daw/ui/Tokens.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace daw::app
{

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        // First, and before anything is built: this process may have been
        // launched to scan one plugin, not to be the application. A scanner that
        // constructed an Engine, an Edit and a window would be a second DAW
        // fighting for the audio device.
        if (engine::EngineHost::runAsPluginScannerIfAsked(commandLine))
            return;

        // A log file, so that what the application did can be read after the
        // fact: --demo and --scan report through it, and a plugin that refuses
        // to load says why.
        logger_.reset(juce::FileLogger::createDefaultAppLogger(
            getApplicationName(), "daw.log", getApplicationName() + " " + getApplicationVersion()));
        juce::Logger::setCurrentLogger(logger_.get());

        const auto domainVersion = domain::versionString();
        juce::Logger::writeToLog("core domain " + juce::String(domainVersion.data(), domainVersion.size()));

        engineHost_ = std::make_unique<engine::EngineHost>(getApplicationName());
        projector_ = std::make_unique<engine::ProjectProjector>(
            engineHost_->edit(), state_, &engineHost_->catalogue(), &engineHost_->stateStore());
        bus_.addObserver(*projector_);

        // The bridge hangs on the projector, so a plugin's own knob becomes a
        // command with a gesture around it. It is built after the projector is
        // observing the bus, because it only has work to do once a projection
        // has created the plugins.
        bridge_ = std::make_unique<engine::ParameterBridge>(bus_, state_, engineHost_->edit(), *projector_);

        // The bus is called from the message thread and only from there: the
        // projector mutates the Edit, and Tracktion expects that on this
        // thread. The rule has to be revisited when a Python service starts
        // sending commands over a socket.
        if (commandLine.contains("--scan"))
            scanPlugins();

        if (commandLine.contains("--demo"))
            playDemo(pluginPathFromCommandLine(commandLine));

        window_ = std::make_unique<MainWindow>(getApplicationName(), ui::Tokens::builtIn());
    }

    void shutdown() override
    {
        juce::Logger::setCurrentLogger(nullptr);
        pluginWindow_.reset();
        window_.reset();
        bridge_.reset();
        projector_.reset();
        engineHost_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    // --demo --plugin "<path to a .vst3 or a .clap>"
    [[nodiscard]] static juce::String pluginPathFromCommandLine(const juce::String& commandLine)
    {
        const auto tokens = juce::StringArray::fromTokens(commandLine, true);
        for (int index = 0; index < tokens.size() - 1; ++index)
        {
            if (tokens[index] == "--plugin")
                return tokens[index + 1].unquoted();
        }
        return {};
    }

    void scanPlugins()
    {
        auto& catalogue = engineHost_->catalogue();
        const auto report = catalogue.scan();

        juce::Logger::writeToLog("plugin scan: " + juce::String(report.scanned) + " files, " +
                                 juce::String(report.added) + " added, " + juce::String(report.blacklisted) +
                                 " blacklisted, " + juce::String(catalogue.descriptions().size()) +
                                 " known in total");

        if (const auto saved = catalogue.save(); !saved)
            juce::Logger::writeToLog("plugin list not saved: " + juce::String(saved.error().message));
    }

    // Inserts the plugin at the given path on the track, through the bus. The
    // identifier comes from the catalogue, never from apply().
    [[nodiscard]] bool insertPlugin(domain::TrackId trackId, const juce::String& path)
    {
        auto& catalogue = engineHost_->catalogue();

        // One file, scanned on demand: a --demo run should not have to wait for
        // every plugin on the machine.
        for (auto* format : engineHost_->engine().getPluginManager().pluginFormatManager.getFormats())
        {
            if (format == nullptr || !engine::PluginCatalogue::isHostedFormat(format->getName()))
                continue;

            juce::OwnedArray<juce::PluginDescription> found;
            format->findAllTypesForFile(found, path);

            for (auto* description : found)
            {
                if (description == nullptr)
                    continue;

                engineHost_->engine().getPluginManager().knownPluginList.addType(*description);

                domain::PluginInstance instance{};
                instance.id = domain::PluginId::generate();
                instance.ref = engine::PluginCatalogue::refFor(*description);

                if (!bus_.execute(std::make_unique<domain::InsertPlugin>(trackId, instance, 0)))
                    return false;

                juce::Logger::writeToLog("demo: hosting " + description->name + " (" +
                                         description->pluginFormatName + ")");

                openWindowFor(instance.id);
                return true;
            }
        }

        juce::Logger::writeToLog("demo: no VST3 or CLAP plugin found at " + path);
        static_cast<void>(catalogue);
        return false;
    }

    // Opens the plugin's own window, from the application and on the message
    // thread. The window holds nothing but the plugin's editor.
    void openWindowFor(domain::PluginId pluginId)
    {
        auto* plugin = findProjectedPlugin(pluginId);
        if (plugin == nullptr)
            return;

        if (!PluginWindow::hasEditor(*plugin))
        {
            juce::Logger::writeToLog("demo: this plugin has no editor to show");
            return;
        }

        pluginWindow_ = std::make_unique<PluginWindow>(*plugin, ui::Tokens::builtIn());
        pluginWindow_->onClose = [this] { pluginWindow_.reset(); };

        juce::Logger::writeToLog("demo: plugin window open, " + juce::String(pluginWindow_->getWidth()) +
                                 " by " + juce::String(pluginWindow_->getHeight()));
    }

    [[nodiscard]] tracktion::Plugin* findProjectedPlugin(domain::PluginId pluginId) const
    {
        const juce::Identifier domainPluginIdProperty{"dawDomainPluginId"};
        const auto wanted = juce::String(pluginId.toString());

        for (auto* track : tracktion::getAudioTracks(engineHost_->edit()))
        {
            if (track == nullptr)
                continue;

            for (auto plugin : track->pluginList.getPlugins())
            {
                if (plugin != nullptr &&
                    plugin->state.getProperty(domainPluginIdProperty).toString() == wanted)
                    return plugin;
            }
        }
        return nullptr;
    }

    // One track, one MIDI clip, three notes, playback. Everything goes through
    // the bus, so this is exactly what the UI will do later. With --plugin, the
    // notes are played by the user's own plugin instead of the built-in synth.
    void playDemo(const juce::String& pluginPath)
    {
        domain::Track track{};
        track.id = domain::TrackId::generate();
        track.name = "Demo";

        const auto trackId = track.id;
        if (!state_.addTrack(std::move(track)))
            return;

        projector_->reconcile();

        if (pluginPath.isNotEmpty())
            static_cast<void>(insertPlugin(trackId, pluginPath));

        const auto clipId = domain::ClipId::generate();
        if (!bus_.execute(std::make_unique<domain::CreateMidiClip>(trackId, clipId, 0.0, 4.0)))
            return;

        // A C major arpeggio, one note per beat.
        int beat = 0;
        for (const int pitch : {60, 64, 67})
        {
            domain::Note note{};
            note.id = domain::NoteId::generate();
            note.pitch = pitch;
            note.velocity = 100;
            note.startBeats = static_cast<double>(beat);
            note.lengthBeats = 1.0;
            ++beat;

            if (!bus_.execute(std::make_unique<domain::AddNote>(clipId, note)))
                return;
        }

        for (const auto& missing : projector_->missingPlugins())
            juce::Logger::writeToLog("demo: plugin missing on this machine: " + juce::String(missing));

        if (bus_.execute(std::make_unique<domain::TransportPlay>()))
            juce::Logger::writeToLog("demo: transport running, 3 notes on track " +
                                     juce::String(trackId.toString()));
    }

    domain::ProjectState state_;
    domain::CommandRegistry registry_{domain::CommandRegistry::withBuiltinCommands()};
    domain::CommandBus bus_{state_, registry_};
    std::unique_ptr<engine::EngineHost> engineHost_;
    std::unique_ptr<engine::ProjectProjector> projector_;
    std::unique_ptr<engine::ParameterBridge> bridge_;
    std::unique_ptr<juce::FileLogger> logger_;
    std::unique_ptr<MainWindow> window_;
    std::unique_ptr<PluginWindow> pluginWindow_;
};

} // namespace daw::app

START_JUCE_APPLICATION(daw::app::Application)
