#include "daw/engine/MeterTap.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

const char* MeterTapPlugin::xmlTypeName = "dawMeterTap";
const juce::Identifier MeterTapPlugin::stripProperty{"dawMeterStrip"};
const juce::String MeterTapPlugin::masterStrip{"master"};

MeterTapPlugin::MeterTapPlugin(tracktion::PluginCreationInfo info)
    : tracktion::Plugin(info)
{
}

MeterTapPlugin::~MeterTapPlugin()
{
    notifyListenersOfDeletion();
}

juce::ValueTree MeterTapPlugin::create(const juce::String& strip)
{
    juce::ValueTree tree{tracktion::IDs::PLUGIN};
    tree.setProperty(tracktion::IDs::type, xmlTypeName, nullptr);
    tree.setProperty(stripProperty, strip, nullptr);
    return tree;
}

juce::String MeterTapPlugin::strip() const
{
    return state.getProperty(stripProperty).toString();
}

void MeterTapPlugin::initialise(const tracktion::PluginInitialisationInfo& info)
{
    sampleRate_.store(info.sampleRate, std::memory_order_relaxed);
}

void MeterTapPlugin::clearTotals() noexcept
{
    for (auto& peak : totalPeak_)
        peak.store(0.0f, std::memory_order_relaxed);
    for (auto& sum : totalSumSquares_)
        sum.store(0.0, std::memory_order_relaxed);
    totalSamples_.store(0, std::memory_order_relaxed);
    totalChannels_.store(0, std::memory_order_relaxed);
}

void MeterTapPlugin::applyToBuffer(const tracktion::PluginRenderContext& context)
{
    // The audio thread. Nothing below allocates, locks or waits: fixed arrays,
    // atomics, and a ring that drops a block rather than wait for its reader.
    if (context.destBuffer == nullptr || context.bufferNumSamples <= 0)
        return;

    if (const auto asked = resetsAsked_.load(std::memory_order_acquire);
        asked != resetsDone_.load(std::memory_order_relaxed))
    {
        clearTotals();
        resetsDone_.store(asked, std::memory_order_release);
    }

    const auto& buffer = *context.destBuffer;
    BlockLevel level{};
    level.samples = context.bufferNumSamples;
    level.channels = std::min(buffer.getNumChannels(), BlockLevel::maxChannels);

    const bool audible = audible_.load(std::memory_order_relaxed);

    for (int channel = 0; audible && channel < level.channels; ++channel)
    {
        const auto* data = buffer.getReadPointer(channel, context.bufferStartSample);
        float peak = 0.0f;
        float sum = 0.0f;
        for (int index = 0; index < level.samples; ++index)
        {
            const auto sample = data[index];
            peak = std::max(peak, std::abs(sample));
            sum += sample * sample;
        }

        const auto slot = static_cast<std::size_t>(channel);
        level.peak[slot] = peak;
        level.sumSquares[slot] = sum;

        // One writer: a load and a store, never a read-modify-write that
        // another thread could interleave with.
        totalPeak_[slot].store(std::max(totalPeak_[slot].load(std::memory_order_relaxed), peak),
                               std::memory_order_relaxed);
        totalSumSquares_[slot].store(totalSumSquares_[slot].load(std::memory_order_relaxed) + sum,
                                     std::memory_order_relaxed);
    }

    totalSamples_.store(totalSamples_.load(std::memory_order_relaxed) + level.samples,
                        std::memory_order_relaxed);
    totalChannels_.store(std::max(totalChannels_.load(std::memory_order_relaxed), level.channels),
                         std::memory_order_release);

    const auto write = write_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % ringCapacity;
    if (next == read_.load(std::memory_order_acquire))
    {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    ring_[static_cast<std::size_t>(write)] = level;
    write_.store(next, std::memory_order_release);
}

bool MeterTapPlugin::pop(BlockLevel& level) noexcept
{
    const auto read = read_.load(std::memory_order_relaxed);
    if (read == write_.load(std::memory_order_acquire))
        return false;

    level = ring_[static_cast<std::size_t>(read)];
    read_.store((read + 1) % ringCapacity, std::memory_order_release);
    return true;
}

TapTotals MeterTapPlugin::totals() const noexcept
{
    TapTotals totals{};
    if (resetsDone_.load(std::memory_order_acquire) != resetsAsked_.load(std::memory_order_acquire))
        return totals; // reset asked, not carried out: nothing heard since

    totals.channels = totalChannels_.load(std::memory_order_acquire);
    for (std::size_t slot = 0; slot < totals.peak.size(); ++slot)
    {
        totals.peak[slot] = totalPeak_[slot].load(std::memory_order_relaxed);
        totals.sumSquares[slot] = totalSumSquares_[slot].load(std::memory_order_relaxed);
    }
    totals.samples = totalSamples_.load(std::memory_order_relaxed);
    return totals;
}

} // namespace daw::engine
