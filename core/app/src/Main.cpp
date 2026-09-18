#include "MainWindow.h"
#include "daw/domain/BuildInfo.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/EngineHost.h"
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
        const auto domainVersion = domain::versionString();
        juce::Logger::writeToLog("core domain " + juce::String(domainVersion.data(), domainVersion.size()));

        engineHost_ = std::make_unique<engine::EngineHost>(getApplicationName());
        projector_ = std::make_unique<engine::ProjectProjector>(engineHost_->edit(), state_);
        bus_.addObserver(*projector_);

        // The bus is called from the message thread and only from there: the
        // projector mutates the Edit, and Tracktion expects that on this
        // thread. The rule has to be revisited when a Python service starts
        // sending commands over a socket.
        if (commandLine.contains("--demo"))
            playDemo();

        window_ = std::make_unique<MainWindow>(getApplicationName(), ui::Tokens::builtIn());
    }

    void shutdown() override
    {
        window_.reset();
        projector_.reset();
        engineHost_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    // One track, one MIDI clip, three notes, playback. Everything goes through
    // the bus, so this is exactly what the UI will do later.
    void playDemo()
    {
        domain::Track track{};
        track.id = domain::TrackId::generate();
        track.name = "Demo";

        const auto trackId = track.id;
        if (!state_.addTrack(std::move(track)))
            return;

        projector_->reconcile();

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

        if (bus_.execute(std::make_unique<domain::TransportPlay>()))
            juce::Logger::writeToLog("demo: transport running, 3 notes on track " +
                                     juce::String(trackId.toString()));
    }

    domain::ProjectState state_;
    domain::CommandRegistry registry_{domain::CommandRegistry::withBuiltinCommands()};
    domain::CommandBus bus_{state_, registry_};
    std::unique_ptr<engine::EngineHost> engineHost_;
    std::unique_ptr<engine::ProjectProjector> projector_;
    std::unique_ptr<MainWindow> window_;
};

} // namespace daw::app

START_JUCE_APPLICATION(daw::app::Application)
