#pragma once

#include "daw/domain/Value.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/LevelMeters.h"

#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include <vector>

namespace daw::app
{

// The meters, kept fresh.
//
// Thirty times a second it drains the taps the projector placed, and says
// something moved. What it holds is the reading of the last 300 ms, for the
// screen and for the copilot alike: a meter the copilot cannot read is a mixer
// it cannot judge, and a meter only the copilot can read is a number nobody
// checks.
class LevelMonitor final : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    static constexpr int pollHz = 30;

    explicit LevelMonitor(tracktion::Edit& edit);
    ~LevelMonitor() override;

    LevelMonitor(const LevelMonitor&) = delete;
    LevelMonitor& operator=(const LevelMonitor&) = delete;
    LevelMonitor(LevelMonitor&&) = delete;
    LevelMonitor& operator=(LevelMonitor&&) = delete;

    [[nodiscard]] std::vector<engine::StripLevel> levels() const { return meters_.levels(); }
    [[nodiscard]] engine::LevelMeters& meters() noexcept { return meters_; }

    // What mix.levels answers: every strip of the project, named the way the
    // copilot names it, and the master last. A track that is in the project
    // and not yet in the Edit reads silent rather than missing.
    [[nodiscard]] domain::Value toValue(const domain::ProjectState& state) const;

private:
    void timerCallback() override;

    tracktion::Edit& edit_;
    engine::LevelMeters meters_;
};

} // namespace daw::app
