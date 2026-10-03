#pragma once

#include "daw/engine/AudioOutputKeeper.h"
#include "daw/engine/PluginCatalogue.h"

#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace daw::engine
{

// Owns the Tracktion Engine and the Edit. This is the only place in the whole
// project that constructs an Engine: the app asks this class, it does not build
// one itself.
//
// The Edit lives in memory and is never written to disk in S3. Saving belongs
// to the versioning layer, which will persist the command journal, not an
// Edit file — the Edit is a projection, not the truth.
class EngineHost
{
public:
    explicit EngineHost(const juce::String& applicationName);
    ~EngineHost();

    EngineHost(const EngineHost&) = delete;
    EngineHost& operator=(const EngineHost&) = delete;
    EngineHost(EngineHost&&) = delete;
    EngineHost& operator=(EngineHost&&) = delete;

    [[nodiscard]] tracktion::Engine& engine() noexcept { return *engine_; }
    [[nodiscard]] tracktion::Edit& edit() noexcept { return *edit_; }

    // The plugins installed on this machine. This one really is machine state:
    // it outlives a project and it is never part of one.
    //
    // The content store used to live here too, and it does not any more: the
    // blobs of a project belong to the project folder, so the application owns
    // the store and hands it to the projector.
    [[nodiscard]] PluginCatalogue& catalogue() noexcept { return *catalogue_; }

    // The sound card, kept open for the whole session (S21): a headset that
    // goes away hands the song to the default output of Windows, and gets it
    // back when it returns. See AudioOutputKeeper.h.
    [[nodiscard]] AudioOutputKeeper& output() noexcept { return *output_; }

    // True when a command line asks this process to be a plugin scanner rather
    // than the application. Call it first in main(): the child process must not
    // build an Engine, an Edit or a window.
    [[nodiscard]] static bool runAsPluginScannerIfAsked(const juce::String& commandLine);

private:
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<tracktion::Edit> edit_;
    std::unique_ptr<PluginCatalogue> catalogue_;

    // Last, so the first to go: it listens to the engine's device manager.
    std::unique_ptr<AudioOutputKeeper> output_;
};

} // namespace daw::engine
