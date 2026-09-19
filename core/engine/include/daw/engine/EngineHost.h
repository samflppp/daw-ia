#pragma once

#include "daw/engine/PluginCatalogue.h"
#include "daw/engine/PluginStateStore.h"

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

    // The plugins installed on this machine, and the store holding their opaque
    // states. Both are machine state: they outlive a project and they are never
    // part of one.
    [[nodiscard]] PluginCatalogue& catalogue() noexcept { return *catalogue_; }
    [[nodiscard]] PluginStateStore& stateStore() noexcept { return *stateStore_; }

    // True when a command line asks this process to be a plugin scanner rather
    // than the application. Call it first in main(): the child process must not
    // build an Engine, an Edit or a window.
    [[nodiscard]] static bool runAsPluginScannerIfAsked(const juce::String& commandLine);

private:
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<tracktion::Edit> edit_;
    std::unique_ptr<PluginCatalogue> catalogue_;
    std::unique_ptr<PluginStateStore> stateStore_;
};

} // namespace daw::engine
