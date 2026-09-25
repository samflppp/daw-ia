#include "AppShellView.h"
#include "CopilotBridge.h"
#include "EditClock.h"
#include "MainWindow.h"
#include "PluginRack.h"
#include "PluginWindow.h"
#include "SampleLibrary.h"
#include "TransportSync.h"
#include "Verification.h"
#include "WorkspaceSwitch.h"
#include "daw/domain/BuildInfo.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/EngineHost.h"
#include "daw/engine/ParameterBridge.h"
#include "daw/engine/ProjectProjector.h"
#include "daw/persistence/ProjectStore.h"
#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/GalleryView.h"
#include "daw/ui/PanelRegistry.h"
#include "daw/ui/TitleBarView.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/WorkspaceView.h"
#include "daw/ui/Workspaces.h"
#include "daw/ui/model/History.h"
#include "daw/ui/model/ProjectObserver.h"
#include "daw/ui/model/Selection.h"

#include <juce_gui_extra/juce_gui_extra.h>
#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <optional>

namespace daw::app
{

// The timer is the autosave: the commands are already on disk, one
// transaction each, but they sit in the write-ahead log until a checkpoint
// moves them into the database file. Without it, a project folder copied while
// the session runs would carry a -wal the copy cannot be trusted without.
//
// The same tick reads back what the store could not write. An observer cannot
// return a Result, so a failed write is kept by the store; showing it only
// when the application quits would mean telling the user about a loss they can
// no longer avoid.
class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // One instance, except for the moment the File menu hands over to the next
    // one: the process that opens the other project starts while this one is
    // still closing, and says so.
    bool moreThanOneInstanceAllowed() override { return getCommandLineParameters().contains("--relaunched"); }

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

        // The history panel is fed by the bus like everything else, and it is
        // listening before the journal is replayed: a project reopened must
        // show what was done to it, not an empty list over a full undo stack.
        bus_.addObserver(history_);

        // The project folder comes before everything that touches the project:
        // the content store lives inside it, and the journal is what the state
        // is rebuilt from.
        if (!openProject(projectFolderFromCommandLine(commandLine)))
            return;

        projector_ = std::make_unique<engine::ProjectProjector>(
            engineHost_->edit(), state_, &engineHost_->catalogue(), contentStore_.get());
        bus_.addObserver(*projector_);

        // The interface observes the bus like the projector does, and for the
        // same reason: it is told that something changed, then reads the whole
        // state. No panel is notified of what a command did.
        bus_.addObserver(projectObserver_);
        clock_ = std::make_unique<EditClock>(engineHost_->edit());
        rack_ = std::make_unique<PluginRack>(
            engineHost_->edit(), engineHost_->catalogue(), ui::Tokens::builtIn());

        // The Edit is a projection, so it is built from the state the journal
        // just rebuilt, in one pass rather than one per replayed command.
        projector_->reconcile();

        // The playback graph and the audio device are built here rather than on
        // the first press of play. Measured on this machine, the first play()
        // held the message thread for 3.2 seconds and every later one for
        // 0.3 ms: the cost is real, it happens once, and the only question is
        // whether the user pays it while the window is opening or in the
        // middle of a beat.
        {
            const auto before = juce::Time::getMillisecondCounterHiRes();
            engineHost_->edit().getTransport().ensureContextAllocated();
            juce::Logger::writeToLog("engine: playback context ready in " +
                                     juce::String(juce::Time::getMillisecondCounterHiRes() - before, 1) +
                                     " ms");
        }

        // Recording starts only now: the replay above must not be written back
        // into the journal it came from.
        store_->startRecording(bus_);

        // The bridge hangs on the projector, so a plugin's own knob becomes a
        // command with a gesture around it. It is built after the projector is
        // observing the bus, because it only has work to do once a projection
        // has created the plugins.
        bridge_ = std::make_unique<engine::ParameterBridge>(bus_, state_, engineHost_->edit(), *projector_);

        // The engine stops on its own at the end of the material; without this
        // the domain would go on saying "playing" over a silent engine.
        transportSync_ = std::make_unique<TransportSync>(bus_, state_, engineHost_->edit());

        // The beatmaker opens in pattern mode, on the first pattern: what a
        // beatmaker hears first is the loop being written, not the song. The
        // rack moves the audition to whatever pattern it shows, and a project
        // without a pattern yet auditions nothing until it has one.
        static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetMode>(
            domain::PlayMode::pattern,
            state_.patterns().empty() ? domain::PatternId{} : state_.patterns().front().id)));

        // The bus is called from the message thread and only from there: the
        // projector mutates the Edit, and Tracktion expects that on this
        // thread. The rule has to be revisited when a Python service starts
        // sending commands over a socket.
        if (commandLine.contains("--scan"))
            scanPlugins();

        if (commandLine.contains("--demo"))
            playDemo(pluginPathFromCommandLine(commandLine));

        // One look for the whole process, plugin windows included: a JUCE
        // component built anywhere asks the default look and feel, so setting
        // it here is what keeps a dialog from arriving in JUCE grey.
        lookAndFeel_ = std::make_unique<ui::DawLookAndFeel>(ui::Tokens::builtIn());
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        const auto ownTitleBar = !commandLine.contains("--gallery");
        window_ = std::make_unique<MainWindow>(
            getApplicationName(), ui::Tokens::builtIn(), makeShell(commandLine), ownTitleBar);
        startTimer(autosaveIntervalMs);

        // --no-copilot exists for the runs where a child process would be in
        // the way: the plugin scan, the demo, and a machine with no key.
        if (copilot_ != nullptr && !commandLine.contains("--no-copilot"))
            copilot_->start();

        startVerificationIfAsked(commandLine);
    }

    // --verify, --verify-reopen, --verify-legacy <folder>: the checks a person
    // would run, run by the binary on its own window. See Verification.h.
    void startVerificationIfAsked(const juce::String& commandLine)
    {
        const auto tokens = juce::StringArray::fromTokens(commandLine, true);

        for (int index = 0; index < tokens.size() - 1; ++index)
        {
            auto run = Verification::Run::list;
            if (tokens[index] == "--verify-reopen")
                run = Verification::Run::reopen;
            else if (tokens[index] == "--verify-legacy")
                run = Verification::Run::legacy;
            else if (tokens[index] != "--verify")
                continue;

            if (view_ == nullptr || titleBar_ == nullptr || window_ == nullptr ||
                window_->getContentComponent() == nullptr)
            {
                juce::Logger::writeToLog("verify: no workspace on screen");
                return;
            }

            verification_ = std::make_unique<Verification>(Verification::Wiring{
                bus_,
                state_,
                *view_,
                history_,
                *copilot_,
                selection_,
                *clock_,
                ui::Tokens::builtIn(),
                engineHost_->edit(),
                *sampleLibrary_,
                *window_,
                *window_->getContentComponent(),
                *titleBar_,
                juce::File{tokens[index + 1].unquoted()},
                run,
                [](bool passed)
                {
                    juce::Logger::writeToLog(juce::String("verify: ") + (passed ? "passed" : "FAILED"));
                    juce::JUCEApplication::getInstance()->systemRequestedQuit();
                }});

            verification_->start();
            return;
        }
    }

    void shutdown() override
    {
        stopTimer();
        verification_.reset();

        // The copilot goes first: it holds a thread that answers through the
        // bus, and the bus is about to be taken apart.
        if (copilot_ != nullptr)
            copilot_->stop();

        closeProject();

        juce::Logger::setCurrentLogger(nullptr);

        // The plugin windows go before the Edit that owns the plugins they
        // draw: an editor outliving its plugin by one line is a crash.
        transportSync_.reset();
        rack_.reset();
        window_.reset();

        // After the window: closing it is the last thing that can move a page.
        if (layoutSettings_ != nullptr)
            static_cast<void>(layoutSettings_->saveIfNeeded());
        sampleLibrary_.reset();
        layoutSettings_.reset();

        // Last, once the project is closed and the settings are written: the
        // next process opens the other project and reads the same settings.
        if (relaunchProject_.has_value())
            relaunch(*relaunchProject_);

        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
        switch_.reset();
        copilot_.reset();
        clock_.reset();
        bridge_.reset();
        projector_.reset();
        contentStore_.reset();
        engineHost_.reset();
    }

    void systemRequestedQuit() override
    {
        // Quitting is the one moment where a pending write can still be
        // reported to someone who can act on it.
        if (store_ != nullptr)
        {
            if (const auto saved = store_->save(); !saved)
                juce::Logger::writeToLog("project not saved: " + juce::String(saved.error().message));
        }

        quit();
    }

private:
    static constexpr int autosaveIntervalMs = 30000;

    // --gallery shows every token and every control instead of the workspace.
    // The three hygiene rules cannot see ugliness; this is the surface that is
    // looked at when a token changes.
    // The workspace under the title bar. The gallery keeps the system's title
    // bar: it has no workspace to switch and no project to save.
    [[nodiscard]] std::unique_ptr<juce::Component> makeShell(const juce::String& commandLine)
    {
        auto content = makeContent(commandLine);
        if (switch_ == nullptr)
            return content;

        ui::TitleBarView::Actions actions;
        actions.newProject = [this] { chooseNewProject(); };
        actions.openProject = [this] { chooseProjectToOpen(); };
        actions.save = [this] { saveNow(); };
        actions.saveAs = [this] { chooseSaveAs(); };
        actions.minimise = [this]
        {
            if (window_ != nullptr)
                window_->setMinimised(true);
        };
        actions.toggleMaximise = [this]
        {
            if (window_ != nullptr)
                window_->setFullScreen(!window_->isFullScreen());
        };
        actions.maximised = [this] { return window_ != nullptr && window_->isFullScreen(); };
        actions.close = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };

        auto titleBar = std::make_unique<ui::TitleBarView>(
            ui::Tokens::builtIn(), *lookAndFeel_, *switch_, std::move(actions));
        titleBar_ = titleBar.get();
        titleBar_->setProjectName(projectName());

        // The switch now has two things to tell: the view rebuilds, the bar
        // lights the workspace shown.
        auto show = switch_->onShow;
        switch_->onShow = [this, show](const ui::WorkspaceManifest& asked)
        {
            if (show)
                show(asked);
            if (titleBar_ != nullptr)
                titleBar_->refresh();
        };

        return std::make_unique<AppShellView>(ui::Tokens::builtIn(), std::move(titleBar), std::move(content));
    }

    [[nodiscard]] juce::String projectName() const
    {
        if (store_ == nullptr)
            return {};
        return juce::String::fromUTF8(store_->folder().name().c_str())
            .upToLastOccurrenceOf(".dawproj", false, true);
    }

    [[nodiscard]] static juce::File projectsFolder()
    {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("DAW IA");
    }

    [[nodiscard]] static juce::File withProjectExtension(const juce::File& file)
    {
        return file.hasFileExtension("dawproj") ? file : file.withFileExtension("dawproj");
    }

    static void tell(const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, message);
    }

    // "Enregistrer": the commands are on disk already, one transaction each;
    // this moves them out of the write-ahead log now rather than at the next
    // autosave, so the folder can be copied the second after.
    void saveNow()
    {
        if (store_ == nullptr)
            return;

        const auto saved = store_->save();
        if (!saved)
        {
            reportProjectUnsaved(juce::String(saved.error().message));
            return;
        }

        reportProjectHealthy();
        if (titleBar_ != nullptr)
            titleBar_->setStatus(juce::String(u8"enregistré"), false);
    }

    void chooseNewProject()
    {
        static_cast<void>(projectsFolder().createDirectory());
        chooser_ = std::make_unique<juce::FileChooser>(
            "Nouveau projet",
            projectsFolder().getNonexistentChildFile("Sans titre", ".dawproj"),
            "*.dawproj");

        chooser_->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser)
            {
                if (chooser.getResult() == juce::File{})
                    return;

                const auto target = withProjectExtension(chooser.getResult());
                if (target.exists())
                {
                    tell("Nouveau projet",
                         target.getFileName() +
                             juce::String(u8" existe déjà : « Ouvrir » pour le reprendre."));
                    return;
                }

                switchTo(target);
            });
    }

    void chooseProjectToOpen()
    {
        chooser_ = std::make_unique<juce::FileChooser>("Ouvrir un projet", projectsFolder());

        chooser_->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& chooser)
            {
                const auto folder = chooser.getResult();
                if (folder == juce::File{})
                    return;

                const persistence::ProjectFolder project{folder.getFullPathName().toStdString()};
                if (!juce::File{juce::String{project.databaseFile().string()}}.existsAsFile())
                {
                    tell("Ouvrir un projet",
                         folder.getFileName() +
                             juce::String(u8" n'est pas un projet DAW IA : choisis un dossier .dawproj."));
                    return;
                }

                switchTo(folder);
            });
    }

    // "Enregistrer sous": a copy of the whole folder — journal, history and
    // samples — then the work goes on in the copy, as in any editor.
    void chooseSaveAs()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Enregistrer sous",
            projectsFolder().getNonexistentChildFile(projectName() + " copie", ".dawproj"),
            "*.dawproj");

        chooser_->launchAsync(juce::FileBrowserComponent::saveMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [this](const juce::FileChooser& chooser)
                              {
                                  if (chooser.getResult() != juce::File{})
                                      saveAs(withProjectExtension(chooser.getResult()));
                              });
    }

    void saveAs(const juce::File& target)
    {
        if (store_ == nullptr)
            return;

        if (target.exists())
        {
            tell("Enregistrer sous", target.getFileName() + juce::String(u8" existe déjà."));
            return;
        }

        if (const auto saved = store_->save(); !saved)
        {
            tell("Enregistrer sous", juce::String(saved.error().message));
            return;
        }

        const juce::File source{juce::String{store_->folder().root().string()}};
        if (!source.copyDirectoryTo(target))
        {
            static_cast<void>(target.deleteRecursively());
            tell("Enregistrer sous",
                 juce::String(u8"la copie vers ") + target.getFullPathName() + juce::String(u8" a échoué."));
            return;
        }

        switchTo(target);
    }

    // The application is built around one project, from the journal up, so
    // another project is another process: this one saves and closes, and the
    // last thing it does is start the next one on the chosen folder.
    void switchTo(const juce::File& folder)
    {
        relaunchProject_ = folder;
        if (switch_ != nullptr)
            lastWorkspace_ = juce::String{switch_->current()};
        systemRequestedQuit();
    }

    void relaunch(const juce::File& folder) const
    {
        juce::String arguments = "--relaunched --project \"" + folder.getFullPathName() + "\"";
        if (lastWorkspace_.isNotEmpty())
            arguments << " --workspace " << lastWorkspace_;

        const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        if (!executable.startAsProcess(arguments))
            juce::Logger::writeToLog("relaunch failed: " + executable.getFullPathName() + " " + arguments);
    }

    [[nodiscard]] std::unique_ptr<juce::Component> makeContent(const juce::String& commandLine)
    {
        const auto& tokens = ui::Tokens::builtIn();

        if (commandLine.contains("--gallery"))
            return std::make_unique<ui::GalleryView>(tokens, *lookAndFeel_);

        const auto& workspaces = ui::Workspaces::builtIn();

        // A manifest the binary carries and this build cannot read is said out
        // loud at startup, not discovered by a user who asks for that screen.
        for (const auto& rejected : workspaces.rejected())
            juce::Logger::writeToLog("workspace not loaded: " + rejected);

        const auto wanted = workspaceFromCommandLine(commandLine);
        const auto* manifest = workspaces.find(wanted.toStdString());

        if (manifest == nullptr)
        {
            juce::Logger::writeToLog("no such workspace: " + wanted);
            manifest = workspaces.find(ui::Workspaces::defaultId());
        }

        switch_ = std::make_unique<WorkspaceSwitch>(
            workspaces, manifest != nullptr ? manifest->id : std::string{ui::Workspaces::defaultId()});

        // The copilot is built before the panels, because one of them reads
        // it, and started after: a process that answers before there is a
        // panel to show the answer has nowhere to put it.
        copilot_ = std::make_unique<CopilotBridge>(
            CopilotBridge::Wiring{bus_, state_, registry_, [this] { return rack_->available(); }});

        // Where the pages of a windowed workspace were left, kept next to the
        // other settings of this machine and never in the project: a window's
        // place is a matter of this screen.
        juce::PropertiesFile::Options layoutOptions;
        layoutOptions.applicationName = "DAW IA";
        layoutOptions.folderName = "DAW IA";
        layoutOptions.filenameSuffix = ".layout";
        layoutOptions.osxLibrarySubFolder = "Application Support";
        layoutSettings_ = std::make_unique<juce::PropertiesFile>(layoutOptions);

        // The samples: bytes into the project's store, folders into the same
        // settings as the pages, because both are this machine's.
        sampleLibrary_ =
            std::make_unique<SampleLibrary>([this] { return contentStore_.get(); }, layoutSettings_.get());

        const ui::PanelServices services{tokens,
                                         *lookAndFeel_,
                                         bus_,
                                         state_,
                                         projectObserver_,
                                         selection_,
                                         *clock_,
                                         history_,
                                         *rack_,
                                         *switch_,
                                         *copilot_,
                                         *sampleLibrary_};

        auto view = std::make_unique<ui::WorkspaceView>(services, panelRegistry_);
        view_ = view.get();

        view->setPageMemory(layoutSettings_.get());
        auto* viewPointer = view.get();

        // The switch rebuilds the screen through the view, and the view is what
        // the window owns: the panel that asked never learns either fact.
        switch_->onShow = [viewPointer](const ui::WorkspaceManifest& asked) { viewPointer->show(asked); };

        if (manifest != nullptr)
        {
            view->show(*manifest);
            juce::Logger::writeToLog("workspace " + juce::String(manifest->id) + ": " +
                                     juce::String(static_cast<int>(manifest->placedPanels().size())) +
                                     " panels");
        }

        return view;
    }

    // --workspace <id>. Without it, the default one.
    [[nodiscard]] static juce::String workspaceFromCommandLine(const juce::String& commandLine)
    {
        const auto tokens = juce::StringArray::fromTokens(commandLine, true);
        for (int index = 0; index < tokens.size() - 1; ++index)
        {
            if (tokens[index] == "--workspace")
                return tokens[index + 1].unquoted();
        }

        return juce::String(ui::Workspaces::defaultId().data(), ui::Workspaces::defaultId().size());
    }

    void timerCallback() override
    {
        if (store_ == nullptr)
            return;

        const auto saved = store_->save();
        if (saved)
        {
            reportProjectHealthy();
            return;
        }

        reportProjectUnsaved(juce::String(saved.error().message));
    }

    // No panel exists yet to hold a message, so the title bar carries it: it is
    // the only surface the application has, and a loss the user never sees is
    // worse than a title that is ugly.
    void reportProjectUnsaved(const juce::String& reason)
    {
        if (unsavedReported_)
            return;

        unsavedReported_ = true;
        juce::Logger::writeToLog("project not saved: " + reason);

        if (window_ != nullptr)
            window_->setName(getApplicationName() + juce::String(u8" - projet non enregistré : ") + reason);
        if (titleBar_ != nullptr)
            titleBar_->setStatus(juce::String(u8"non enregistré : ") + reason, true);
    }

    void reportProjectHealthy()
    {
        if (!unsavedReported_)
            return;

        unsavedReported_ = false;
        if (window_ != nullptr)
            window_->setName(getApplicationName());
        if (titleBar_ != nullptr)
            titleBar_->setStatus({}, false);
    }

    // --project "<path to a .dawproj folder>". Without it, the application
    // opens the same default project every time, which is what a first launch
    // needs and what --demo relies on.
    [[nodiscard]] juce::File projectFolderFromCommandLine(const juce::String& commandLine)
    {
        const auto tokens = juce::StringArray::fromTokens(commandLine, true);
        for (int index = 0; index < tokens.size() - 1; ++index)
        {
            if (tokens[index] == "--project")
                return juce::File{tokens[index + 1].unquoted()};
        }

        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(getApplicationName())
            .getChildFile("Sans titre.dawproj");
    }

    // Opens the folder, rebuilds the state from the journal, and reports what
    // it replayed. A project that fails to open stops the launch: opening a
    // project half-way and letting the user work on it would write a journal
    // on top of a state that is not the one on disk.
    [[nodiscard]] bool openProject(const juce::File& folder)
    {
        auto store = persistence::ProjectStore::open(
            persistence::ProjectFolder{folder.getFullPathName().toStdString()});
        if (!store)
        {
            juce::Logger::writeToLog("project not opened: " + juce::String(store.error().message));
            setApplicationReturnValue(1);
            quit();
            return false;
        }

        store_ = std::move(store).value();
        contentStore_ = std::make_unique<engine::ContentStore>(
            juce::File{juce::String{store_->folder().blobsFolder().string()}});

        auto report = store_->replayInto(bus_);
        if (!report)
        {
            juce::Logger::writeToLog("project not replayed: " + juce::String(report.error().message));
            setApplicationReturnValue(1);
            quit();
            return false;
        }

        juce::Logger::writeToLog("project " + juce::String(store_->folder().name()) + ": " +
                                 juce::String(static_cast<int>(report.value().commands)) +
                                 " commands replayed, " +
                                 juce::String(static_cast<int>(report.value().undone)) + " undone");
        return true;
    }

    void closeProject()
    {
        if (store_ == nullptr)
            return;

        store_->stopRecording();
        if (const auto closed = store_->close(); !closed)
            juce::Logger::writeToLog("project not saved: " + juce::String(closed.error().message));

        store_.reset();
    }

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
        if (rack_ == nullptr)
            return;

        if (!rack_->hasEditor(pluginId))
        {
            juce::Logger::writeToLog("demo: this plugin has no editor to show");
            return;
        }

        rack_->openEditor(pluginId);
        juce::Logger::writeToLog("demo: plugin window open");
    }

    // One track, one MIDI clip, three notes, playback. Everything goes through
    // the bus, so this is exactly what the UI will do later. With --plugin, the
    // notes are played by the user's own plugin instead of the built-in synth.
    void playDemo(const juce::String& pluginPath)
    {
        const auto trackId = domain::TrackId::generate();
        if (!bus_.execute(std::make_unique<domain::AddTrack>(trackId, "Demo")))
            return;

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
    std::unique_ptr<persistence::ProjectStore> store_;
    bool unsavedReported_{false};
    std::unique_ptr<engine::ContentStore> contentStore_;
    std::unique_ptr<engine::EngineHost> engineHost_;
    std::unique_ptr<engine::ProjectProjector> projector_;
    std::unique_ptr<engine::ParameterBridge> bridge_;
    std::unique_ptr<juce::FileLogger> logger_;
    std::unique_ptr<ui::DawLookAndFeel> lookAndFeel_;
    ui::PanelRegistry panelRegistry_{ui::PanelRegistry::withBuiltinPanels()};
    ui::ProjectObserver projectObserver_;
    ui::Selection selection_;
    ui::History history_;
    std::unique_ptr<EditClock> clock_;
    std::unique_ptr<WorkspaceSwitch> switch_;
    std::unique_ptr<CopilotBridge> copilot_;
    std::unique_ptr<PluginRack> rack_;
    std::unique_ptr<TransportSync> transportSync_;
    std::unique_ptr<juce::PropertiesFile> layoutSettings_;
    std::unique_ptr<SampleLibrary> sampleLibrary_;
    ui::WorkspaceView* view_{nullptr};
    std::unique_ptr<Verification> verification_;
    ui::TitleBarView* titleBar_{nullptr};
    std::unique_ptr<juce::FileChooser> chooser_;
    std::optional<juce::File> relaunchProject_;
    juce::String lastWorkspace_;
    std::unique_ptr<MainWindow> window_;
};

} // namespace daw::app

START_JUCE_APPLICATION(daw::app::Application)
