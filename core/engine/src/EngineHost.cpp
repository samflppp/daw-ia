#include "daw/engine/EngineHost.h"

#include "daw/engine/ClapPluginFormat.h"

namespace daw::engine
{

bool EngineHost::runAsPluginScannerIfAsked(const juce::String& commandLine)
{
    // Tracktion's own child-process scanner. When this returns true the process
    // is a scanner: it talks to its parent over a pipe and nothing else.
    return tracktion::PluginManager::startChildProcessPluginScan(commandLine);
}

EngineHost::EngineHost(const juce::String& applicationName)
    : engine_{std::make_unique<tracktion::Engine>(applicationName)}
{
    // Tracktion opens the audio device while the Engine is being constructed,
    // but it builds its list of wave devices from an async update, so the list
    // is still empty when the constructor returns. An Edit created and played
    // before that update has run is bound to no output at all: the transport
    // reports that it is running, and not one sample leaves the machine.
    //
    // Flushing it here is what makes the constructor mean what it says: when
    // it returns, the engine is usable. The alternative — waiting for the
    // message loop — would make "is the engine ready" depend on who calls it
    // and when, which is exactly the kind of question a caller should not have
    // to ask.
    engine_->getDeviceManager().dispatchPendingUpdates();

    auto& pluginManager = engine_->getPluginManager();

    // CLAP hosting is ours: neither JUCE nor Tracktion knows the format. Added
    // here, it becomes just another juce::AudioPluginFormat, so scanning,
    // instantiation and the projection treat VST3 and CLAP the same way.
    pluginManager.pluginFormatManager.addFormat(std::make_unique<ClapPluginFormat>());

    // A plugin that crashes must take a scanner process down, never the DAW.
    pluginManager.setUsesSeparateProcessForScanning(true);

    catalogue_ =
        std::make_unique<PluginCatalogue>(*engine_, PluginCatalogue::defaultListFile(applicationName));
    catalogue_->load();

    stateStore_ = std::make_unique<PluginStateStore>(PluginStateStore::defaultRoot(applicationName));

    edit_ = std::make_unique<tracktion::Edit>(*engine_, tracktion::Edit::forEditing);

    // Tracktion has its own UndoManager. It stays unused: undo belongs to the
    // Command Bus, and two histories would drift apart the first time one of
    // them was asked to go back. Every mutation below passes nullptr where an
    // UndoManager is expected.
    edit_->getUndoManager().clearUndoHistory();
}

EngineHost::~EngineHost() = default;

} // namespace daw::engine
