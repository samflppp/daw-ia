#include "daw/engine/MixTap.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>

namespace daw::engine
{
namespace
{

const juce::Identifier keyProperty{"dawMixTapKey"};
const juce::Identifier endProperty{"dawMixTapEnd"};

std::mutex& registryLock()
{
    static std::mutex lock;
    return lock;
}

std::map<juce::String, domain::mix::StreamAnalyser*>& registry()
{
    static std::map<juce::String, domain::mix::StreamAnalyser*> analysers;
    return analysers;
}

} // namespace

const char* MixTap::xmlTypeName = "dawMixTap";

MixTap::Registration::Registration(juce::String key)
    : key_{std::move(key)}
{
}

MixTap::Registration::~Registration()
{
    if (key_.isEmpty())
        return;
    const std::lock_guard guard{registryLock()};
    registry().erase(key_);
}

MixTap::Registration::Registration(Registration&& other) noexcept
    : key_{std::exchange(other.key_, {})}
{
}

MixTap::Registration MixTap::enrol(const juce::String& key, domain::mix::StreamAnalyser& analyser)
{
    const std::lock_guard guard{registryLock()};
    registry()[key] = &analyser;
    return Registration{key};
}

juce::ValueTree MixTap::create(const juce::String& key, double endSeconds)
{
    juce::ValueTree tree{tracktion::IDs::PLUGIN};
    tree.setProperty(tracktion::IDs::type, xmlTypeName, nullptr);
    tree.setProperty(keyProperty, key, nullptr);
    tree.setProperty(endProperty, endSeconds, nullptr);
    return tree;
}

MixTap::MixTap(tracktion::PluginCreationInfo info)
    : tracktion::Plugin(info)
{
}

MixTap::~MixTap()
{
    notifyListenersOfDeletion();
}

void MixTap::initialise(const tracktion::PluginInitialisationInfo& info)
{
    sampleRate_ = info.sampleRate;
    endSeconds_ = static_cast<double>(state.getProperty(endProperty));
    const std::lock_guard guard{registryLock()};
    const auto found = registry().find(state.getProperty(keyProperty).toString());
    analyser_ = found != registry().end() ? found->second : nullptr;
}

void MixTap::applyToBuffer(const tracktion::PluginRenderContext& context)
{
    if (analyser_ == nullptr || context.destBuffer == nullptr || context.bufferNumSamples <= 0)
        return;
    const auto& buffer = *context.destBuffer;
    if (buffer.getNumChannels() == 0)
        return;

    // Only the song: not the blocks the renderer runs before 0, nor past
    // the end. A block that crosses the end gives what lies before it.
    const auto start = context.editTime.getStart().inSeconds();
    if (start < 0.0 || start >= endSeconds_)
        return;
    const auto left = static_cast<int>(std::ceil((endSeconds_ - start) * sampleRate_));
    const auto count = std::min(context.bufferNumSamples, left);
    if (count <= 0)
        return;
    const auto* first = buffer.getReadPointer(0, context.bufferStartSample);
    const auto* second =
        buffer.getNumChannels() > 1 ? buffer.getReadPointer(1, context.bufferStartSample) : first;
    analyser_->process(first, second, static_cast<std::size_t>(count));
}

} // namespace daw::engine
