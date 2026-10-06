#include "daw/engine/EngineHost.h"

#include "daw/engine/ClapPluginFormat.h"
#include "daw/engine/LiveInput.h"
#include "daw/engine/MeterTap.h"
#include "daw/engine/MixTap.h"

namespace daw::engine
{

bool EngineHost::runAsPluginScannerIfAsked(const juce::String& commandLine)
{
    // Tracktion's own child-process scanner. When this returns true the process
    // is a scanner: it talks to its parent over a pipe and nothing else.
    return tracktion::PluginManager::startChildProcessPluginScan(commandLine);
}

namespace
{

// Tracktion's settings, kept in a folder given rather than in %APPDATA%.
class FolderPropertyStorage final : public tracktion::PropertyStorage
{
public:
    FolderPropertyStorage(const juce::String& applicationName, juce::File folder)
        : tracktion::PropertyStorage{applicationName}
        , folder_{std::move(folder)}
    {
    }

    juce::File getAppPrefsFolder() override
    {
        if (!folder_.isDirectory())
            folder_.createDirectory();
        return folder_;
    }

private:
    juce::File folder_;
};

juce::File personalFolder(const juce::String& applicationName)
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(applicationName);
}

} // namespace

EngineHost::EngineHost(const juce::String& applicationName)
    : EngineHost{applicationName, personalFolder(applicationName)}
{
}

EngineHost::EngineHost(const juce::String& applicationName, const juce::File& settingsFolder)
    : engine_{std::make_unique<tracktion::Engine>(
          std::make_unique<FolderPropertyStorage>(applicationName, settingsFolder), nullptr, nullptr)}
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
    output_ = std::make_unique<AudioOutputKeeper>(engine_->getDeviceManager().deviceManager);
    output_->onLost = [this] { live_.silence(domain::live::now()); };
    audio_ = std::make_unique<AudioSettings>(
        engine_->getDeviceManager().deviceManager, *output_, settingsFolder.getChildFile("carte-audio.json"));
    audio_->beforeReopen = [this] { live_.silence(domain::live::now()); };

    auto& pluginManager = engine_->getPluginManager();

    // CLAP hosting is ours: neither JUCE nor Tracktion knows the format. Added
    // here, it becomes just another juce::AudioPluginFormat, so scanning,
    // instantiation and the projection treat VST3 and CLAP the same way.
    pluginManager.pluginFormatManager.addFormat(std::make_unique<ClapPluginFormat>());

    // The level taps the projector places at the end of every chain. A type
    // Tracktion has to know before an Edit can hold one.
    pluginManager.createBuiltInType<MeterTapPlugin>();

    // The notes played live (S23), first in the chain of every track.
    pluginManager.createBuiltInType<LiveInputPlugin>();

    // The ear of the mix measurement (S20), only ever in a copy of the Edit.
    pluginManager.createBuiltInType<MixTap>();

    // A plugin that crashes must take a scanner process down, never the DAW.
    pluginManager.setUsesSeparateProcessForScanning(true);

    catalogue_ = std::make_unique<PluginCatalogue>(*engine_, settingsFolder.getChildFile("plugins.xml"));
    catalogue_->load();

    edit_ = std::make_unique<tracktion::Edit>(*engine_, tracktion::Edit::forEditing);

    // Tracktion has its own UndoManager. It stays unused: undo belongs to the
    // Command Bus, and two histories would drift apart the first time one of
    // them was asked to go back. Every mutation below passes nullptr where an
    // UndoManager is expected.
    edit_->getUndoManager().clearUndoHistory();
}

EngineHost::~EngineHost() = default;

} // namespace daw::engine
