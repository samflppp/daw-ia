#include "MixComparison.h"

#include <algorithm>
#include <cmath>

namespace daw::app
{
namespace
{

constexpr int readAheadSamples = 24000;
constexpr int stereo = 2;
constexpr int smallestScratch = 4096; // samples: larger than any device block seen so far

} // namespace

MixComparison::MixComparison(juce::AudioDeviceManager& device)
    : device_(device)
{
    formats_.registerBasicFormats();
    readAhead_.startThread();
    device_.addAudioCallback(this);
}

MixComparison::~MixComparison()
{
    device_.removeAudioCallback(this);
    unload();
    readAhead_.stopThread(2000);
}

bool MixComparison::load(const juce::File& before,
                         double beforeLufs,
                         const juce::File& after,
                         double afterLufs)
{
    unload();
    auto* first = formats_.createReaderFor(before);
    auto* second = formats_.createReaderFor(after);
    if (first == nullptr || second == nullptr)
    {
        delete first;
        delete second;
        return false;
    }
    const auto rateBefore = first->sampleRate;
    const auto rateAfter = second->sampleRate;
    before_.source = std::make_unique<juce::AudioFormatReaderSource>(first, true);
    afterSide_.source = std::make_unique<juce::AudioFormatReaderSource>(second, true);
    before_.transport.setSource(before_.source.get(), readAheadSamples, &readAhead_, rateBefore);
    afterSide_.transport.setSource(afterSide_.source.get(), readAheadSamples, &readAhead_, rateAfter);

    // The louder one comes down to the other.
    const auto difference = afterLufs - beforeLufs;
    beforeGainDb_ = difference < 0.0 ? difference : 0.0;
    afterGainDb_ = difference > 0.0 ? -difference : 0.0;
    beforeGain_.store(static_cast<float>(juce::Decibels::decibelsToGain(beforeGainDb_)));
    afterGain_.store(static_cast<float>(juce::Decibels::decibelsToGain(afterGainDb_)));
    return true;
}

void MixComparison::unload()
{
    stop();
    before_.transport.setSource(nullptr);
    afterSide_.transport.setSource(nullptr);
    before_.source.reset();
    afterSide_.source.reset();
}

void MixComparison::play(double seconds, bool after)
{
    if (before_.source == nullptr || afterSide_.source == nullptr)
        return;
    select(after);
    for (int side = 0; side < 2; ++side)
    {
        heardSum_[side].store(0.0);
        heardCount_[side].store(0);
    }
    before_.transport.setPosition(seconds);
    afterSide_.transport.setPosition(seconds);
    before_.transport.start();
    afterSide_.transport.start();
}

void MixComparison::stop()
{
    before_.transport.stop();
    afterSide_.transport.stop();
}

bool MixComparison::playing() const
{
    return before_.transport.isPlaying();
}

double MixComparison::heardRmsDb(bool after) const noexcept
{
    const auto side = after ? 1 : 0;
    const auto count = heardCount_[side].load();
    if (count == 0)
        return -100.0;
    const auto meanSquare = heardSum_[side].load() / static_cast<double>(count);
    return meanSquare > 0.0 ? 10.0 * std::log10(meanSquare) : -100.0;
}

void MixComparison::audioDeviceIOCallbackWithContext(const float* const* inputs,
                                                     int inputCount,
                                                     float* const* outputs,
                                                     int outputCount,
                                                     int samples,
                                                     const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(inputs, inputCount, context);
    juce::AudioBuffer<float> view{outputs, outputCount, samples};
    view.clear();
    if (!before_.transport.isPlaying())
        return;

    // Both advance together, whichever is heard: switching never jumps.
    const bool after = after_.load(std::memory_order_relaxed);
    // The renders are stereo, and go to the first two outputs, where the song
    // goes (Tracktion's default « Output channel 1 + 2 »). A card with more
    // outputs — the speakers of the founder's laptop declare eight — used to
    // be skipped whole, block after block: the comparison was silent there
    // (S21). The others stay silent.
    const auto heard = std::min(outputCount, stereo);
    for (auto* side : {&before_, &afterSide_})
    {
        if (side->scratch.getNumSamples() < samples)
            continue; // sized in audioDeviceAboutToStart; a larger block is skipped, never allocated here
        juce::AudioBuffer<float> into{side->scratch.getArrayOfWritePointers(), stereo, samples};
        side->transport.getNextAudioBlock(juce::AudioSourceChannelInfo{&into, 0, samples});
        const auto index = side == &afterSide_ ? 1 : 0;
        const auto gain = index == 1 ? afterGain_.load() : beforeGain_.load();
        if ((index == 1) == after)
        {
            for (int channel = 0; channel < heard; ++channel)
                view.copyFromWithRamp(channel, 0, into.getReadPointer(channel), samples, gain, gain);
            double sum = 0.0;
            for (int channel = 0; channel < heard; ++channel)
            {
                const auto rms = view.getRMSLevel(channel, 0, samples);
                sum += static_cast<double>(rms) * rms * samples;
            }
            heardSum_[index].store(heardSum_[index].load() + sum / std::max(1, heard));
            heardCount_[index].store(heardCount_[index].load() + samples);
        }
    }
}

void MixComparison::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const auto block = device->getCurrentBufferSizeSamples();
    const auto rate = device->getCurrentSampleRate();
    for (auto* side : {&before_, &afterSide_})
    {
        side->scratch.setSize(stereo, std::max(block, smallestScratch));
        side->transport.prepareToPlay(block, rate);
    }
}

void MixComparison::audioDeviceStopped()
{
    before_.transport.releaseResources();
    afterSide_.transport.releaseResources();
}

} // namespace daw::app
