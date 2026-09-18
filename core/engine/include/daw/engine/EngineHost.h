#pragma once

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

private:
    std::unique_ptr<tracktion::Engine> engine_;
    std::unique_ptr<tracktion::Edit> edit_;
};

} // namespace daw::engine
