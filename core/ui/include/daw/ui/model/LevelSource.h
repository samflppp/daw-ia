#pragma once

#include <juce_events/juce_events.h>

#include <string>
#include <vector>

namespace daw::ui
{

// What a meter on screen shows: the levels of the last 300 ms, per strip.
//
// An interface for the same reason as TransportClock: core/ui knows nothing
// of Tracktion, and the application answers it over the engine's taps. It
// broadcasts each time the levels were read again, about thirty times a
// second.
struct StripMeter
{
    static constexpr float floorDb = -100.0f;

    std::string strip; // a domain TrackId, or "master"
    float peakLeftDb{floorDb};
    float peakRightDb{floorDb};
    float rmsLeftDb{floorDb};
    float rmsRightDb{floorDb};
    bool over{false};
};

class LevelSource : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* masterStrip = "master";

    LevelSource() = default;
    ~LevelSource() override = default;

    LevelSource(const LevelSource&) = delete;
    LevelSource& operator=(const LevelSource&) = delete;
    LevelSource(LevelSource&&) = delete;
    LevelSource& operator=(LevelSource&&) = delete;

    [[nodiscard]] virtual std::vector<StripMeter> meters() const = 0;

    // The over lights are latched; a click on one puts them all out.
    virtual void clearOvers() = 0;
};

} // namespace daw::ui
