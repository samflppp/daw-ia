#pragma once

#include "daw/domain/live/AudioAdvice.h"
#include "daw/engine/AudioOutputKeeper.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace daw::engine
{

// How the blocks really reach the card (S24): a callback of its own on the
// device, which writes silence and times each call. What the « Audio » window
// shows as measured — the buffer the card plays and its dropouts — comes from
// here, not from what the driver declares.
//
// One writer, the audio thread; one reader, the message thread. Nothing is
// allocated or locked in the callback. A reset is asked by the reader and
// carried out by the writer at its next block, like MeterTapPlugin's.
class BlockClock final : public juce::AudioIODeviceCallback
{
public:
    [[nodiscard]] domain::live::BlockTiming timing() const noexcept;
    void reset() noexcept { resetsAsked_.fetch_add(1, std::memory_order_acq_rel); }

    // Every block since the clock was made, never reset: read twice, it says
    // whether the card plays at all.
    [[nodiscard]] std::int64_t blocksSeen() const noexcept { return seen_.load(std::memory_order_acquire); }

    // When a card last stopped or started, on the millisecond counter: a
    // card reopened — chosen, lost, given back — stops the engine's
    // transport for a moment, and that moment is not the song ending.
    [[nodiscard]] double changedAtMs() const noexcept { return changedAtMs_.load(std::memory_order_relaxed); }

private:
    void audioDeviceIOCallbackWithContext(const float* const* inputs,
                                          int inputCount,
                                          float* const* outputs,
                                          int outputCount,
                                          int samples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    std::atomic<double> rate_{48000.0};
    std::atomic<double> changedAtMs_{0.0};
    std::atomic<std::int64_t> lastTicks_{0};
    std::atomic<std::int64_t> seen_{0};
    std::atomic<std::int64_t> blocks_{0};
    std::atomic<std::int64_t> intervals_{0};
    std::atomic<double> sumSeconds_{0.0};
    std::atomic<double> worstSeconds_{0.0};
    std::atomic<std::int64_t> late_{0};
    std::atomic<int> lastSize_{0};
    std::atomic<std::uint32_t> resetsAsked_{0};
    std::atomic<std::uint32_t> resetsDone_{0};
};

// The driver, the output and the buffer of the sound card, as the « Audio »
// window chooses them (S24). A machine setting: Tracktion keeps it in its
// settings file (DEVICESETUP in Settings.xml), never in the project.
//
// Choosing:
//   - what plays live is silenced first (beforeReopen), exactly as when a
//     card is lost: no note is left sounding through the reopening;
//   - the keeper is told before the device opens, so that its own look does
//     not take the card back to the old one;
//   - only the output is opened: nothing here records, and an input in the
//     same setup would fail the exclusive and low-latency modes for a
//     microphone nobody uses;
//   - a setup JUCE refuses gives the old one back, and says why;
//   - a setup that opens and plays no block within half a second gives the
//     old one back too, and says so.
//
// The advice is measured (decided on 6 October 2026, see AudioAdvice.h):
// startTrial() opens each setup in turn, lets it settle a second, measures
// its blocks for three, then gives the first setup back. The trials are
// kept in `store`, a file of this machine, by output: the advice reads them
// without trying again until the card is tried again.
//
// The message thread only.
class AudioSettings final : private juce::Timer
{
public:
    struct Choice
    {
        juce::String type;
        juce::String output;
        int buffer{0};

        friend bool operator==(const Choice&, const Choice&) = default;
    };

    AudioSettings(juce::AudioDeviceManager& devices, AudioOutputKeeper& keeper, juce::File store);
    ~AudioSettings() override;

    AudioSettings(const AudioSettings&) = delete;
    AudioSettings& operator=(const AudioSettings&) = delete;
    AudioSettings(AudioSettings&&) = delete;
    AudioSettings& operator=(AudioSettings&&) = delete;

    // The WASAPI drivers this machine offers, shared first. DirectSound is
    // left out: see domain/live/AudioAdvice.h.
    [[nodiscard]] juce::StringArray types() const;
    [[nodiscard]] juce::StringArray outputs(const juce::String& type) const;

    // The sizes JUCE offers for that output in that driver. For the device
    // that is open, read from it; for another, from a device made and never
    // opened, kept for the session (asking a driver takes a moment).
    [[nodiscard]] std::vector<int> buffers(const juce::String& type, const juce::String& output) const;

    // What the card plays with now, read from the engine.
    [[nodiscard]] Choice current() const;
    [[nodiscard]] double sampleRate() const;
    [[nodiscard]] double outputLatencySeconds() const;

    [[nodiscard]] domain::live::AudioAdvice advice() const;

    // --- trying the card
    void startTrial();
    void cancelTrial();
    [[nodiscard]] bool trialRunning() const noexcept { return trial_.has_value(); }
    [[nodiscard]] double trialProgress() const noexcept; // 0..1
    // The trials kept for the output now open, empty before one.
    [[nodiscard]] std::vector<domain::live::CardTrial> trials() const;

    // Opens the card with that choice. False, and the old setup back, when
    // it does not open; said() tells what happened either way.
    bool apply(const Choice& choice);
    [[nodiscard]] const juce::String& said() const noexcept { return said_; }

    [[nodiscard]] domain::live::BlockTiming timing() const noexcept { return clock_.timing(); }
    void resetTiming() noexcept { clock_.reset(); }
    [[nodiscard]] std::int64_t blocksSeen() const noexcept { return clock_.blocksSeen(); }
    [[nodiscard]] double cardChangedAtMs() const noexcept { return clock_.changedAtMs(); }

    // Silences what plays live before the card closes.
    std::function<void()> beforeReopen;

    // Told whenever said() changes, including half a second after an
    // apply, when a silent card was given back.
    std::function<void()> onChanged;

private:
    void timerCallback() override;
    bool open(const Choice& choice, juce::String& error);
    void say(const juce::String& what);

    // The trial runs on a timer of its own: the class's timer is the half
    // second that checks a setup just opened.
    struct TrialTicks final : juce::Timer
    {
        explicit TrialTicks(AudioSettings& owner)
            : owner_(owner)
        {
        }
        void timerCallback() override { owner_.trialTick(); }
        AudioSettings& owner_;
    };
    struct Trial
    {
        Choice first;
        std::vector<domain::live::TrialSetup> setups;
        std::vector<domain::live::CardTrial> results;
        std::size_t index{0};
        bool opened{false};
        double settleUntilMs{0.0};
        double measuredFromMs{0.0};
    };
    void trialTick();
    void finishTrial(bool cancelled);
    void loadStore();
    void saveStore() const;

    juce::AudioDeviceManager& devices_;
    AudioOutputKeeper& keeper_;
    BlockClock clock_;

    // A setup just opened, waiting to be heard: the one to go back to, and
    // the blocks seen when it opened.
    struct Pending
    {
        Choice previous;
        Choice tried;
        std::int64_t seenAtOpen{0};
    };
    std::optional<Pending> pending_;
    juce::String said_;

    juce::File store_;
    std::map<juce::String, std::vector<domain::live::CardTrial>> kept_; // by output
    std::optional<Trial> trial_;
    TrialTicks ticks_{*this};

    mutable std::map<std::pair<juce::String, juce::String>, std::vector<int>> sizes_;
};

} // namespace daw::engine
