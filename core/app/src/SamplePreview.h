#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

namespace daw::app
{

// Listening to a sample of the machine before dropping it: FL's browser does
// it on a click, and a beatmaker picks a kick by ear, not by name.
//
// Outside the project on purpose. Nothing is imported, nothing is journalled,
// nothing sounds in a render: the sample goes straight to the speakers, as a
// second callback of the audio device the engine already opened. JUCE sums
// the callbacks of one device, so the song and the preview are heard together.
//
// The file is read ahead on a thread of its own, so the audio callback never
// touches the disk. It does take the lock JUCE's AudioTransportSource holds
// while a source is swapped — a few instructions, on a click, never during a
// steady play — which is said here rather than hidden.
class SamplePreview final : private juce::AudioIODeviceCallback
{
public:
    explicit SamplePreview(juce::AudioDeviceManager& device);
    ~SamplePreview() override;

    SamplePreview(const SamplePreview&) = delete;
    SamplePreview& operator=(const SamplePreview&) = delete;
    SamplePreview(SamplePreview&&) = delete;
    SamplePreview& operator=(SamplePreview&&) = delete;

    // Plays the file from its start, stopping whatever was playing. False for
    // a file JUCE cannot read.
    bool play(const juce::File& file);
    void stop();

    [[nodiscard]] juce::File current() const { return current_; }

    // The loudest sample sent to the speakers since the sample started, in
    // dBFS: what the verification measures, since it cannot listen.
    [[nodiscard]] float peakDb() const noexcept;

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs,
                                          int inputCount,
                                          float* const* outputs,
                                          int outputCount,
                                          int samples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager& device_;
    juce::AudioFormatManager formats_;
    juce::TimeSliceThread readAhead_{"sample preview"};
    juce::AudioTransportSource transport_;
    std::unique_ptr<juce::AudioFormatReaderSource> source_;
    juce::File current_;
    std::atomic<float> peak_{0.0f};
};

} // namespace daw::app
