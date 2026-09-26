#include "SamplePreview.h"

#include <algorithm>
#include <cmath>

namespace daw::app
{
namespace
{

// Samples read ahead of the playhead: a quarter of a second at 48 kHz.
constexpr int readAheadSamples = 12000;

} // namespace

SamplePreview::SamplePreview(juce::AudioDeviceManager& device)
    : device_(device)
{
    formats_.registerBasicFormats();
    readAhead_.startThread();
    device_.addAudioCallback(this);
}

SamplePreview::~SamplePreview()
{
    device_.removeAudioCallback(this);
    transport_.setSource(nullptr);
    source_.reset();
    readAhead_.stopThread(2000);
}

bool SamplePreview::play(const juce::File& file)
{
    auto* reader = formats_.createReaderFor(file);
    if (reader == nullptr)
        return false;

    auto next = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    transport_.stop();
    peak_.store(0.0f, std::memory_order_relaxed);
    transport_.setSource(next.get(), readAheadSamples, &readAhead_, reader->sampleRate);
    source_ = std::move(next);
    current_ = file;
    transport_.setPosition(0.0);
    transport_.start();
    return true;
}

void SamplePreview::stop()
{
    transport_.stop();
    transport_.setSource(nullptr);
    source_.reset();
    current_ = juce::File{};
}

float SamplePreview::peakDb() const noexcept
{
    const auto peak = peak_.load(std::memory_order_relaxed);
    return peak > 0.0f ? std::max(-100.0f, 20.0f * std::log10(peak)) : -100.0f;
}

void SamplePreview::audioDeviceIOCallbackWithContext(const float* const* inputs,
                                                     int inputCount,
                                                     float* const* outputs,
                                                     int outputCount,
                                                     int samples,
                                                     const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(inputs, inputCount, context);

    // Written, not added: JUCE hands each extra callback a buffer of its own
    // and sums them afterwards. A view over those pointers allocates nothing.
    juce::AudioBuffer<float> view{outputs, outputCount, samples};
    const juce::AudioSourceChannelInfo block{&view, 0, samples};
    transport_.getNextAudioBlock(block);
    // The loudest since the sample started: a drum hit lasts a few blocks,
    // and whoever reads the level may look a little later.
    peak_.store(std::max(peak_.load(std::memory_order_relaxed), view.getMagnitude(0, samples)),
                std::memory_order_relaxed);
}

void SamplePreview::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    transport_.prepareToPlay(device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
}

void SamplePreview::audioDeviceStopped()
{
    transport_.releaseResources();
}

} // namespace daw::app
