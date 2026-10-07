#include "daw/engine/FluxTap.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

const char* FluxTapPlugin::xmlTypeName = "dawFluxTap";
const juce::Identifier FluxTapPlugin::stripProperty{"dawFluxStrip"};
const juce::Identifier FluxTapPlugin::slotProperty{"dawFluxSlot"};
const juce::Identifier FluxTapPlugin::companionProperty{"dawFluxCompanion"};
const juce::String FluxTapPlugin::sourceSlot{"source"};
const juce::String FluxTapPlugin::faderSlot{"fader"};

FluxTapPlugin::FluxTapPlugin(tracktion::PluginCreationInfo info)
    : tracktion::Plugin(info)
{
}

FluxTapPlugin::~FluxTapPlugin()
{
    notifyListenersOfDeletion();
}

juce::ValueTree FluxTapPlugin::create(const juce::String& strip, const juce::String& slot, bool companion)
{
    juce::ValueTree tree{tracktion::IDs::PLUGIN};
    tree.setProperty(tracktion::IDs::type, xmlTypeName, nullptr);
    tree.setProperty(stripProperty, strip, nullptr);
    tree.setProperty(slotProperty, slot, nullptr);
    tree.setProperty(companionProperty, companion, nullptr);
    return tree;
}

juce::String FluxTapPlugin::strip() const
{
    return state.getProperty(stripProperty).toString();
}

juce::String FluxTapPlugin::slot() const
{
    return state.getProperty(slotProperty).toString();
}

bool FluxTapPlugin::companion() const
{
    return static_cast<bool>(state.getProperty(companionProperty));
}

void FluxTapPlugin::initialise(const tracktion::PluginInitialisationInfo& info)
{
    sampleRate_.store(info.sampleRate, std::memory_order_relaxed);
}

void FluxTapPlugin::applyToBuffer(const tracktion::PluginRenderContext& context)
{
    // The audio thread: fixed buffers and atomics, nothing else.
    const auto started = juce::Time::getHighResolutionTicks();
    blocks_.store(blocks_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

    const bool armed = armed_.load(std::memory_order_relaxed);
    const bool capturing = capturing_.load(std::memory_order_acquire);
    if ((armed || capturing) && context.destBuffer != nullptr && context.bufferNumSamples > 0)
    {
        const auto& buffer = *context.destBuffer;
        const auto channels = std::max(1, buffer.getNumChannels());
        const auto rate = sampleRate_.load(std::memory_order_relaxed);
        const auto first =
            static_cast<std::int64_t>(std::llround(context.editTime.getStart().inSeconds() * rate));
        const bool audible = audible_.load(std::memory_order_relaxed);
        const auto scale = audible ? 1.0f / static_cast<float>(channels) : 0.0f;
        const auto captureSize = static_cast<std::int64_t>(capture_.size());

        for (int index = 0; index < context.bufferNumSamples; ++index)
        {
            float mono = 0.0f;
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                mono += buffer.getSample(channel, context.bufferStartSample + index);
            mono *= scale;

            const auto position = first + index;
            if (armed)
                ring_[static_cast<std::size_t>(position & (ringSize - 1))] = mono;
            if (capturing && position >= 0 && position < captureSize)
                capture_[static_cast<std::size_t>(position)] = mono;
        }
        if (armed)
            latest_.store(first + context.bufferNumSamples - 1, std::memory_order_release);
    }

    busyTicks_.store(busyTicks_.load(std::memory_order_relaxed) +
                         (juce::Time::getHighResolutionTicks() - started),
                     std::memory_order_relaxed);
}

void FluxTapPlugin::read(std::int64_t from, int count, float* out) const noexcept
{
    const auto last = latest_.load(std::memory_order_acquire);
    for (int index = 0; index < count; ++index)
    {
        const auto position = from + index;
        // Only what the ring still holds: not past the last write, not a
        // whole turn behind it.
        out[index] = (position <= last && position > last - ringSize && position >= 0)
                         ? ring_[static_cast<std::size_t>(position & (ringSize - 1))]
                         : 0.0f;
    }
}

void FluxTapPlugin::startCapture(int samples)
{
    capturing_.store(false, std::memory_order_release);
    capture_.assign(static_cast<std::size_t>(std::max(0, samples)), 0.0f);
    capturing_.store(true, std::memory_order_release);
}

double FluxTapPlugin::busySeconds() const noexcept
{
    return juce::Time::highResolutionTicksToSeconds(busyTicks_.load(std::memory_order_relaxed));
}

void FluxTapPlugin::resetCost() noexcept
{
    busyTicks_.store(0, std::memory_order_relaxed);
    blocks_.store(0, std::memory_order_relaxed);
}

} // namespace daw::engine
