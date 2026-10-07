#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace daw::engine
{

// The sound at one place of a chain, for the audio flux (S24): a tap placed
// by the projector at every state of the graph — after a channel's
// instrument, after each effect, after the fader, at a bus's sum.
//
// It measures nothing and draws nothing. When its node is on the screen, it
// is armed, and it writes what passes, mono (the mean of its channels),
// into a ring indexed by the edit's sample position: the window reads the
// last second or so by position, and two taps of the same place — a track
// and its companion — are added sample for sample. Not armed, it reads one
// atomic per block and returns.
//
// The position is the edit time Tracktion hands the plugin, which it moves
// back by the latency of every plugin before it (PluginNode's automation
// adjustment): the before and the after of an effect that delays the sound
// read at the same instant, without the tap knowing the latency.
//
// One writer, the audio thread; readers on the message thread. Nothing is
// allocated, locked or waited for while it plays: the ring is made with the
// tap, a capture (for a test or a render) is allocated before it starts.
class FluxTapPlugin final : public tracktion::Plugin
{
public:
    static const char* xmlTypeName;

    // Which place it taps: the strip (a domain TrackId, or "master"), the
    // slot ("source", "fader", or the domain id of the effect it follows),
    // and whether it sits on a track's companion (its recordings).
    static const juce::Identifier stripProperty;
    static const juce::Identifier slotProperty;
    static const juce::Identifier companionProperty;
    static const juce::String sourceSlot;
    static const juce::String faderSlot;

    explicit FluxTapPlugin(tracktion::PluginCreationInfo info);
    ~FluxTapPlugin() override;

    FluxTapPlugin(const FluxTapPlugin&) = delete;
    FluxTapPlugin& operator=(const FluxTapPlugin&) = delete;
    FluxTapPlugin(FluxTapPlugin&&) = delete;
    FluxTapPlugin& operator=(FluxTapPlugin&&) = delete;

    [[nodiscard]] static juce::ValueTree
    create(const juce::String& strip, const juce::String& slot, bool companion);
    [[nodiscard]] juce::String strip() const;
    [[nodiscard]] juce::String slot() const;
    [[nodiscard]] bool companion() const;

    // --- tracktion::Plugin
    juce::String getName() const override { return "DAW IA flux"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getShortName(int) override { return "Flux"; }
    juce::String getSelectableDescription() override { return "DAW IA flux"; }
    bool canBeDisabled() override { return false; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }
    void initialise(const tracktion::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void applyToBuffer(const tracktion::PluginRenderContext& context) override;

    // Silent when the strip is not heard: Tracktion mutes a track after its
    // chain, and the state after the fader is what leaves the strip.
    void setAudible(bool audible) noexcept { audible_.store(audible, std::memory_order_relaxed); }

    // --- the reader, on the message thread

    void arm(bool armed) noexcept { armed_.store(armed, std::memory_order_relaxed); }
    [[nodiscard]] bool armed() const noexcept { return armed_.load(std::memory_order_relaxed); }

    static constexpr int ringSize = 1 << 16; // 1.37 s at 48 kHz

    // The last position written, and the samples of a span of positions
    // that the ring still holds (older ones read 0). `out` has `count` values.
    [[nodiscard]] std::int64_t latest() const noexcept { return latest_.load(std::memory_order_acquire); }
    void read(std::int64_t from, int count, float* out) const noexcept;
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }

    // Every sample from position 0 to `samples`, kept whole: for a render
    // read afterwards, where the ring would have turned many times. The
    // buffer is made here, on the message thread; capturing() is then read
    // back once the render is over.
    void startCapture(int samples);
    void stopCapture() noexcept { capturing_.store(false, std::memory_order_release); }
    [[nodiscard]] const std::vector<float>& captured() const noexcept { return capture_; }

    // The time the audio thread spent in this tap since the last reset, in
    // seconds, and the blocks: what the flux costs, measured where it is paid.
    [[nodiscard]] double busySeconds() const noexcept;
    [[nodiscard]] std::int64_t blocks() const noexcept { return blocks_.load(std::memory_order_relaxed); }
    void resetCost() noexcept;

private:
    std::vector<float> ring_ = std::vector<float>(ringSize, 0.0f);
    std::atomic<std::int64_t> latest_{-1};
    std::atomic<bool> armed_{false};
    std::atomic<bool> audible_{true};
    std::atomic<double> sampleRate_{48000.0};

    std::vector<float> capture_;
    std::atomic<bool> capturing_{false};

    std::atomic<std::int64_t> busyTicks_{0};
    std::atomic<std::int64_t> blocks_{0};
};

} // namespace daw::engine
