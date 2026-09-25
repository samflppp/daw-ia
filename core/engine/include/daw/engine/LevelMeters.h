#pragma once

#include "daw/engine/MeterTap.h"

#include <tracktion_engine/tracktion_engine.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace daw::engine
{

// What one strip sounds like: a domain track, or the master.
//
// In dBFS, floored at -100 dB like the fader: a number a copilot can compare,
// never an infinity. Left and right are kept apart because a balance cannot be
// judged from their sum; the combined figures are what a single meter shows.
struct StripLevel
{
    static constexpr float floorDb = -100.0f;

    std::string strip; // a domain TrackId, or "master"
    float peakDb{floorDb};
    float rmsDb{floorDb};
    float peakLeftDb{floorDb};
    float peakRightDb{floorDb};
    float rmsLeftDb{floorDb};
    float rmsRightDb{floorDb};

    // How long the figures above were measured over: the window for levels(),
    // the whole stretch for totals(). Zero when nothing was heard at all.
    double seconds{0.0};

    // A sample reached 0 dBFS since the overs were last cleared. Latched: a
    // clip that lasted one block must still be there when someone looks.
    bool over{false};
};

// The levels of every strip, read on the message thread from the taps the
// projector placed.
//
// Two readings, for two questions:
//
//   levels()   what is sounding now: peak held and RMS integrated over the
//              last 300 ms, the window of a classic VU. Fed by poll(), which a
//              timer calls; the taps' rings are drained there and nowhere else.
//
//   totals()   what sounded since resetTotals(): peak and RMS over the whole
//              stretch. A render is read this way — nobody polls during it —
//              and so is any "measure this passage" a copilot will ask for.
//
// A domain track that plays both an instrument and recordings is two Tracktion
// tracks, so two taps. Their peaks combine as the larger one and their RMS as a
// sum of powers: exact when only one of them sounds, which is the common case,
// and an estimate when both do, since the peak of a sum is not recoverable from
// the peaks of its parts.
class LevelMeters final
{
public:
    static constexpr double windowSeconds = 0.3;

    explicit LevelMeters(tracktion::Edit& edit);

    // Drains every tap. Message thread only.
    void poll();

    [[nodiscard]] std::vector<StripLevel> levels() const;
    void clearOvers();

    void resetTotals();
    [[nodiscard]] std::vector<StripLevel> totals() const;

    // Blocks the taps had to drop since they were created. Stays at zero while
    // someone polls; the verification checks that it does.
    [[nodiscard]] std::size_t droppedBlocks() const;

    // Every tap the Edit holds, on the tracks and on the master.
    [[nodiscard]] static std::vector<MeterTapPlugin*> tapsOf(tracktion::Edit& edit);

private:
    struct Window
    {
        juce::String tapId;
        std::string strip;
        std::deque<BlockLevel> blocks;
        std::int64_t samples{0};
        double sampleRate{44100.0};
        double lastBlockMs{0.0};
        bool over{false};
    };

    [[nodiscard]] Window& windowFor(const MeterTapPlugin& tap);

    tracktion::Edit& edit_;
    std::vector<Window> windows_;
};

} // namespace daw::engine
