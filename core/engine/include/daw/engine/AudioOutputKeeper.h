#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <functional>

namespace daw::engine
{

// Keeps a sound card open for as long as the application runs (S21).
//
// The silent playback of S12 and S20 was this: the output — a Bluetooth
// headset — went away during the session. JUCE closes a device that has gone
// and tries the one it remembers, and when that fails it leaves no device
// open at all; from then on it reopens nothing, not even when a card comes
// back, because it only looks again while a device is open. The transport
// went on "playing" for minutes without a single block reaching the speakers.
//
// What it does instead, the way every other application of the machine does:
//   - no device open: the one the session started on if it is there, the
//     default output of Windows otherwise, tried again every second until one
//     opens;
//   - a fallback open and the first one back: back to the first one.
// The song is not stopped: Tracktion restarts its playback on the new device.
// Every change goes to the log and to onChanged, which the screen shows.
//
// The message thread only.
class AudioOutputKeeper final : private juce::ChangeListener, private juce::Timer
{
public:
    explicit AudioOutputKeeper(juce::AudioDeviceManager& devices);
    ~AudioOutputKeeper() override;

    AudioOutputKeeper(const AudioOutputKeeper&) = delete;
    AudioOutputKeeper& operator=(const AudioOutputKeeper&) = delete;
    AudioOutputKeeper(AudioOutputKeeper&&) = delete;
    AudioOutputKeeper& operator=(AudioOutputKeeper&&) = delete;

    // The output now open, empty when there is none.
    [[nodiscard]] juce::String outputName() const;

    // The output the session started on, which it goes back to.
    [[nodiscard]] const juce::String& preferredName() const noexcept { return preferred_; }

    // How many times an output was reopened after being lost, or given back.
    [[nodiscard]] int reopenCount() const noexcept { return reopened_; }

    // Said once per change: « sortie perdue », « sortie : Haut-parleurs ».
    std::function<void(const juce::String& what)> onChanged;

    // What a lost card leaves behind, made on purpose: the device closed the
    // way JUCE closes one that has gone. For --verify-lecture; the next look,
    // within a second, must reopen it.
    void loseOutputForTest();

    // Looks now rather than at the next tick.
    void check();

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    [[nodiscard]] bool available(const juce::String& name) const;
    [[nodiscard]] juce::String windowsDefault() const;
    bool open(const juce::String& name);
    void say(const juce::String& what);

    juce::AudioDeviceManager& devices_;
    juce::String preferred_;
    juce::String typeName_;
    bool lost_{false};
    bool opening_{false};
    int reopened_{0};
};

} // namespace daw::engine
