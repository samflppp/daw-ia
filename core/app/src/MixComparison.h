#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <memory>

namespace daw::app
{

// Before and after the mix by the AI (S20), at equal loudness.
//
// Both are renders already made — the session measured, the proposal
// verified — so nothing here touches the project or the Edit that plays: the
// two files run side by side, in step, as a second callback of the audio
// device (like SamplePreview), and a switch picks which one is heard. Switching
// keeps the position: the ear compares the same bar.
//
// The louder one is turned down by what the measure says separates them:
// otherwise the louder always wins, and the comparison lies.
class MixComparison final : private juce::AudioIODeviceCallback
{
public:
    explicit MixComparison(juce::AudioDeviceManager& device);
    ~MixComparison() override;

    MixComparison(const MixComparison&) = delete;
    MixComparison& operator=(const MixComparison&) = delete;
    MixComparison(MixComparison&&) = delete;
    MixComparison& operator=(MixComparison&&) = delete;

    // The two renders and their integrated loudness (LUFS). Stops what was
    // playing. False when a file cannot be read.
    bool load(const juce::File& before, double beforeLufs, const juce::File& after, double afterLufs);
    void unload();

    // Plays from `seconds`, the one asked for audible.
    void play(double seconds, bool after);
    void select(bool after) noexcept { after_.store(after, std::memory_order_relaxed); }
    void stop();

    [[nodiscard]] bool playing() const;
    [[nodiscard]] bool afterSelected() const noexcept { return after_.load(std::memory_order_relaxed); }

    // The gains applied, dB: one of them is 0.
    [[nodiscard]] double beforeGainDb() const noexcept { return beforeGainDb_; }
    [[nodiscard]] double afterGainDb() const noexcept { return afterGainDb_; }

    // What reached the speakers since play(), RMS dBFS, per side: the
    // verification compares them, since it cannot listen.
    [[nodiscard]] double heardRmsDb(bool after) const noexcept;

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs,
                                          int inputCount,
                                          float* const* outputs,
                                          int outputCount,
                                          int samples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    struct Side
    {
        std::unique_ptr<juce::AudioFormatReaderSource> source;
        juce::AudioTransportSource transport;
        juce::AudioBuffer<float> scratch;
    };

    juce::AudioDeviceManager& device_;
    juce::AudioFormatManager formats_;
    juce::TimeSliceThread readAhead_{"mix comparison"};
    Side before_;
    Side afterSide_;
    std::atomic<bool> after_{false};
    double beforeGainDb_{0.0};
    double afterGainDb_{0.0};
    std::atomic<float> beforeGain_{1.0f};
    std::atomic<float> afterGain_{1.0f};
    std::atomic<double> heardSum_[2]{0.0, 0.0};
    std::atomic<std::int64_t> heardCount_[2]{0, 0};
};

} // namespace daw::app
