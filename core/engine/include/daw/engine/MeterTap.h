#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace daw::engine
{

// What one audio block measured: per channel, the loudest sample and the sum of
// the squares, which is what an RMS over any window is rebuilt from.
struct BlockLevel
{
    static constexpr int maxChannels = 2;

    std::array<float, maxChannels> peak{};
    std::array<float, maxChannels> sumSquares{};
    int samples{0};
    int channels{0};
};

// The running totals of a tap since it was last reset: what a render measured,
// read once the render is over.
struct TapTotals
{
    std::array<float, BlockLevel::maxChannels> peak{};
    std::array<double, BlockLevel::maxChannels> sumSquares{};
    std::int64_t samples{0};
    int channels{0};
};

// A level meter that measures and draws nothing: it hands its numbers to the
// message thread.
//
// Tracktion has its own LevelMeasurer, and it is not used, for the one reason
// the S4 parameter bridge gave: it takes a spin lock inside the audio callback
// and may grow a vector there. This tap does neither. Per block it computes a
// peak and a sum of squares, writes them into a ring allocated with the object,
// and moves two indices; the reader is a timer on the message thread.
//
// It is a projection artefact, like the companion track: never in the domain,
// placed by the projector last in every chain, so what it measures is what the
// strip sends out — after the plugins, after the fader, after the pan.
//
// One writer, one reader. The audio thread writes the ring and the totals; the
// message thread reads them. A reset is a request the writer carries out at
// the start of its next block, so the totals only ever have one writer; until
// then the reader sees them as empty.
class MeterTapPlugin final : public tracktion::Plugin
{
public:
    static const char* xmlTypeName;

    // Which strip the tap measures: a domain TrackId, or masterStrip. A track
    // and its companion carry the same one, and their numbers are summed.
    static const juce::Identifier stripProperty;
    static const juce::String masterStrip;

    explicit MeterTapPlugin(tracktion::PluginCreationInfo info);
    ~MeterTapPlugin() override;

    MeterTapPlugin(const MeterTapPlugin&) = delete;
    MeterTapPlugin& operator=(const MeterTapPlugin&) = delete;
    MeterTapPlugin(MeterTapPlugin&&) = delete;
    MeterTapPlugin& operator=(MeterTapPlugin&&) = delete;

    [[nodiscard]] static juce::ValueTree create(const juce::String& strip);

    [[nodiscard]] juce::String strip() const;

    // --- tracktion::Plugin
    juce::String getName() const override { return "DAW IA meter"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getShortName(int) override { return "Meter"; }
    juce::String getSelectableDescription() override { return "DAW IA meter"; }
    bool canBeDisabled() override { return false; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }

    void initialise(const tracktion::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void applyToBuffer(const tracktion::PluginRenderContext& context) override;

    // Whether the strip reaches the mix. Tracktion silences a muted track after
    // its plugin chain, so a tap placed last in the chain still hears it; the
    // projector says here what it decided, and a tap that is not audible
    // reports silence for the blocks it sees. A meter shows what leaves the
    // strip, not what would leave it if the mute were off.
    void setAudible(bool audible) noexcept { audible_.store(audible, std::memory_order_relaxed); }
    [[nodiscard]] bool audible() const noexcept { return audible_.load(std::memory_order_relaxed); }

    // --- the reader, on the message thread
    [[nodiscard]] bool pop(BlockLevel& level) noexcept;
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }
    [[nodiscard]] TapTotals totals() const noexcept;
    void resetTotals() noexcept { resetsAsked_.fetch_add(1, std::memory_order_acq_rel); }

    // Blocks lost because nobody read the ring in time. A live meter polled at
    // 30 Hz never fills it; a render, which nobody polls, does, and that loss
    // is harmless because a render is read from the totals.
    [[nodiscard]] std::size_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    void clearTotals() noexcept;

    // About twenty seconds of 512-sample blocks at 48 kHz.
    static constexpr int ringCapacity = 2048;

    std::array<BlockLevel, ringCapacity> ring_{};
    std::atomic<int> write_{0};
    std::atomic<int> read_{0};
    std::atomic<std::size_t> dropped_{0};

    std::array<std::atomic<float>, BlockLevel::maxChannels> totalPeak_{};
    std::array<std::atomic<double>, BlockLevel::maxChannels> totalSumSquares_{};
    std::atomic<std::int64_t> totalSamples_{0};
    std::atomic<int> totalChannels_{0};

    // A reset is asked by the reader and carried out by the writer. Until the
    // writer has done it, the totals are those of a tap that heard nothing
    // since: a muted track is not processed at all, so a tap that waited for
    // its next block to forget would report what it heard before the mute.
    std::atomic<std::uint32_t> resetsAsked_{0};
    std::atomic<std::uint32_t> resetsDone_{0};

    std::atomic<double> sampleRate_{44100.0};
    std::atomic<bool> audible_{true};

    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<double>::is_always_lock_free);
    static_assert(std::atomic<std::int64_t>::is_always_lock_free);
};

} // namespace daw::engine
