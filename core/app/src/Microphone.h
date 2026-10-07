#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace daw::app
{

// The push-to-talk's microphone (S25, decided on 7 October 2026).
//
// Never the engine's: the engine's device keeps its input empty (S21), and
// the microphone is a capture device of its own, WASAPI in shared mode,
// opened when the key goes down and closed when it comes up. Nothing reads
// a sample without the key: the device is not even open.
//
// Threads, one by one:
//   - the capture device's thread (WASAPI, JUCE's) copies each block, mixed
//     to mono, into a buffer made at construction — no lock, no allocation —
//     and keeps the block's peak;
//   - the message thread opens, closes, reads the level, and on close turns
//     what was heard into 16 kHz (a windowed sinc), then hands it over;
//   - the transcription runs in another process (VoiceService); nothing of it
//     comes near the engine's audio thread.
//
// The voice is kept nowhere: the buffer is cleared at each open and at close.
//
// For the checks without a microphone: injectForTest() makes open() play a
// file into the same buffer, block by block, at the speed of a real device.
class Microphone final : private juce::AudioIODeviceCallback, private juce::HighResolutionTimer
{
public:
    static constexpr double maxSeconds = 15.0;
    static constexpr double rate16k = 16000.0;

    struct Input
    {
        juce::String name;
        bool bluetooth{false};
        bool isDefault{false};
    };

    Microphone();
    ~Microphone() override;

    Microphone(const Microphone&) = delete;
    Microphone& operator=(const Microphone&) = delete;
    Microphone(Microphone&&) = delete;
    Microphone& operator=(Microphone&&) = delete;

    // The microphones of this machine. Message thread.
    [[nodiscard]] std::vector<Input> inputs() const;

    // The one used when the person chose none: Windows' default if it is not
    // a Bluetooth headset, else the first that is not; a Bluetooth one never
    // unless chosen. Empty when there is none.
    [[nodiscard]] static juce::String defaultFrom(const std::vector<Input>& inputs);

    // Opens `name` and starts reading. False, and `error` in French, when it
    // does not open (none, refused by Windows' privacy settings, gone).
    bool open(const juce::String& name, juce::String& error);

    // Stops reading, and gives what was heard at 16 kHz, mono.
    [[nodiscard]] std::vector<float> close();

    // Stops reading and forgets.
    void discard();

    [[nodiscard]] bool isOpen() const noexcept { return open_.load(); }
    // True once the first block arrived: what the screen calls « j'écoute ».
    [[nodiscard]] bool hearing() const noexcept { return written_.load() > 0; }
    [[nodiscard]] double seconds() const noexcept;
    // The loudest sample since the last call, in dBFS. Message thread.
    [[nodiscard]] float takePeakDb() noexcept;
    // Every sample read since this object was made: a check reads it before
    // and after a time without the key.
    [[nodiscard]] std::int64_t samplesEverRead() const noexcept { return everRead_.load(); }
    [[nodiscard]] const juce::String& openedName() const noexcept { return openedName_; }

    // The next opens play `samples` (16 kHz, mono) instead of a device.
    void injectForTest(std::vector<float> samples16k);

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs,
                                          int numInputs,
                                          float* const* outputs,
                                          int numOutputs,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void hiResTimerCallback() override;

    void write(const float* const* channels, int numChannels, int numSamples) noexcept;

    std::unique_ptr<juce::AudioIODeviceType> type_;
    std::unique_ptr<juce::AudioIODevice> device_;
    juce::String openedName_;

    std::vector<float> buffer_; // maxSeconds at 192 kHz, made once
    std::atomic<int> written_{0};
    std::atomic<float> peak_{0.0f};
    std::atomic<double> rate_{48000.0};
    std::atomic<bool> open_{false};
    std::atomic<std::int64_t> everRead_{0};

    std::vector<float> injected_;
    std::size_t injectedAt_{0};
};

} // namespace daw::app
