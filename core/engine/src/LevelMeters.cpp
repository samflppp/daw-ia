#include "daw/engine/LevelMeters.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace daw::engine
{
namespace
{

constexpr std::size_t channelCount = BlockLevel::maxChannels;

[[nodiscard]] float toDb(double gain)
{
    if (gain <= 0.0)
        return StripLevel::floorDb;
    return std::max(StripLevel::floorDb, static_cast<float>(20.0 * std::log10(gain)));
}

// What a strip adds up to, before it becomes decibels: linear peaks and mean
// squares, which are what sums correctly across two taps.
struct Accumulator
{
    std::string strip;
    std::array<double, channelCount> peak{};
    std::array<double, channelCount> power{};
    double seconds{0.0};
    bool over{false};

    void add(const std::array<double, channelCount>& peaks,
             const std::array<double, channelCount>& powers,
             double measuredSeconds)
    {
        seconds = std::max(seconds, measuredSeconds);
        for (std::size_t slot = 0; slot < channelCount; ++slot)
        {
            peak[slot] = std::max(peak[slot], peaks[slot]);
            power[slot] += powers[slot];
        }
    }

    [[nodiscard]] StripLevel level() const
    {
        StripLevel level{};
        level.strip = strip;
        level.peakLeftDb = toDb(peak[0]);
        level.peakRightDb = toDb(peak[1]);
        level.rmsLeftDb = toDb(std::sqrt(power[0]));
        level.rmsRightDb = toDb(std::sqrt(power[1]));
        level.peakDb = toDb(std::max(peak[0], peak[1]));
        level.rmsDb = toDb(std::sqrt((power[0] + power[1]) / 2.0));
        level.seconds = seconds;
        level.over = over || std::max(peak[0], peak[1]) >= 1.0;
        return level;
    }
};

[[nodiscard]] Accumulator& accumulatorFor(std::vector<Accumulator>& strips, const std::string& strip)
{
    const auto found = std::find_if(
        strips.begin(), strips.end(), [&strip](const Accumulator& entry) { return entry.strip == strip; });
    if (found != strips.end())
        return *found;

    strips.push_back(Accumulator{strip, {}, {}, 0.0, false});
    return strips.back();
}

// A mono tap sounds the same in both ears.
template <typename Value>
void spreadMono(std::array<Value, channelCount>& values, int channels)
{
    if (channels == 1)
        values[1] = values[0];
}

} // namespace

LevelMeters::LevelMeters(tracktion::Edit& edit)
    : edit_{edit}
{
}

std::vector<MeterTapPlugin*> LevelMeters::tapsOf(tracktion::Edit& edit)
{
    std::vector<MeterTapPlugin*> taps;

    for (auto* track : tracktion::getAudioTracks(edit))
    {
        if (track == nullptr)
            continue;
        for (auto* tap : track->pluginList.getPluginsOfType<MeterTapPlugin>())
        {
            if (tap != nullptr)
                taps.push_back(tap);
        }
    }

    for (auto* tap : edit.getMasterPluginList().getPluginsOfType<MeterTapPlugin>())
    {
        if (tap != nullptr)
            taps.push_back(tap);
    }

    return taps;
}

LevelMeters::Window& LevelMeters::windowFor(const MeterTapPlugin& tap)
{
    const auto id = tap.itemID.toString();
    const auto found = std::find_if(
        windows_.begin(), windows_.end(), [&id](const Window& window) { return window.tapId == id; });
    if (found != windows_.end())
        return *found;

    Window window{};
    window.tapId = id;
    window.strip = tap.strip().toStdString();
    windows_.push_back(std::move(window));
    return windows_.back();
}

void LevelMeters::poll()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto taps = tapsOf(edit_);

    // A tap the projection took away takes its window with it.
    windows_.erase(std::remove_if(windows_.begin(),
                                  windows_.end(),
                                  [&taps](const Window& window)
                                  {
                                      return std::none_of(taps.begin(),
                                                          taps.end(),
                                                          [&window](const MeterTapPlugin* tap)
                                                          { return tap->itemID.toString() == window.tapId; });
                                  }),
                   windows_.end());

    for (auto* tap : taps)
    {
        auto& window = windowFor(*tap);
        window.strip = tap->strip().toStdString();
        window.sampleRate = tap->sampleRate();

        BlockLevel block{};
        while (tap->pop(block))
        {
            window.blocks.push_back(block);
            window.samples += block.samples;
            window.lastBlockMs = now;
            if (std::max(block.peak[0], block.peak[1]) >= 1.0f)
                window.over = true;
        }

        // Keep the last 300 ms of blocks, and not one block less.
        const auto wanted = static_cast<std::int64_t>(windowSeconds * window.sampleRate);
        while (!window.blocks.empty() && window.samples - window.blocks.front().samples >= wanted)
        {
            window.samples -= window.blocks.front().samples;
            window.blocks.pop_front();
        }

        // A tap no block reached for a whole window is silent, not frozen on
        // the last thing it heard: a stopped device sends nothing at all.
        if (now - window.lastBlockMs > windowSeconds * 1000.0)
        {
            window.blocks.clear();
            window.samples = 0;
        }
    }
}

std::vector<StripLevel> LevelMeters::levels() const
{
    std::vector<Accumulator> strips;

    for (const auto& window : windows_)
    {
        auto& strip = accumulatorFor(strips, window.strip);
        strip.over = strip.over || window.over;

        if (window.samples <= 0)
            continue;

        std::array<double, channelCount> peaks{};
        std::array<double, channelCount> sums{};
        int channels = 0;
        for (const auto& block : window.blocks)
        {
            channels = std::max(channels, block.channels);
            for (std::size_t slot = 0; slot < channelCount; ++slot)
            {
                peaks[slot] = std::max(peaks[slot], static_cast<double>(block.peak[slot]));
                sums[slot] += static_cast<double>(block.sumSquares[slot]);
            }
        }

        std::array<double, channelCount> powers{};
        for (std::size_t slot = 0; slot < channelCount; ++slot)
            powers[slot] = sums[slot] / static_cast<double>(window.samples);

        spreadMono(peaks, channels);
        spreadMono(powers, channels);
        strip.add(peaks, powers, static_cast<double>(window.samples) / window.sampleRate);
    }

    std::vector<StripLevel> levels;
    levels.reserve(strips.size());
    for (const auto& strip : strips)
        levels.push_back(strip.level());
    return levels;
}

void LevelMeters::clearOvers()
{
    for (auto& window : windows_)
        window.over = false;
}

void LevelMeters::resetTotals()
{
    for (auto* tap : tapsOf(edit_))
        tap->resetTotals();
}

std::vector<StripLevel> LevelMeters::totals() const
{
    std::vector<Accumulator> strips;

    for (const auto* tap : tapsOf(edit_))
    {
        auto& strip = accumulatorFor(strips, tap->strip().toStdString());
        const auto totals = tap->totals();
        if (totals.samples <= 0)
            continue;

        std::array<double, channelCount> peaks{};
        std::array<double, channelCount> powers{};
        for (std::size_t slot = 0; slot < channelCount; ++slot)
        {
            peaks[slot] = static_cast<double>(totals.peak[slot]);
            powers[slot] = totals.sumSquares[slot] / static_cast<double>(totals.samples);
        }

        spreadMono(peaks, totals.channels);
        spreadMono(powers, totals.channels);
        strip.add(peaks, powers, static_cast<double>(totals.samples) / tap->sampleRate());
    }

    std::vector<StripLevel> levels;
    levels.reserve(strips.size());
    for (const auto& strip : strips)
        levels.push_back(strip.level());
    return levels;
}

std::size_t LevelMeters::droppedBlocks() const
{
    std::size_t dropped = 0;
    for (const auto* tap : tapsOf(edit_))
        dropped += tap->dropped();
    return dropped;
}

} // namespace daw::engine
