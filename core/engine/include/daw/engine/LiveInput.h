#pragma once

#include "daw/domain/live/Router.h"
#include "daw/domain/live/Timeline.h"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <bitset>

namespace daw::engine
{

// The notes played live (S23), handed to the instrument of a track.
//
// First in the chain of every track that plays notes, before the 4OSC
// fallback, the sampler or the person's instrument. At each block it takes
// what the input threads pushed into its track's queue (domain::live::Router)
// and adds it to the block's MIDI, each message at its place on the regular
// timeline — one block and a margin after it was played (decided 6 October
// 2026). It writes nothing into the project: a projection artefact, like the
// meter tap, placed by the projector, never in the domain.
//
// Tracktion's own ways in were not used: AudioTrack::injectLiveMidiMessage
// takes a lock and only reaches a track that holds clips; its MIDI input
// devices take mutexes on the audio thread and keep their routing in the
// Edit.
//
// What it also does: while a note played live is held, the cuts Tracktion
// makes for its clips — a release of the same pitch, the pedal lifted, an
// all-notes-off when the loop goes round or the song stops — are kept from
// the instrument, so that a chord held over the end of the loop goes on
// sounding until its keys come up.
//
// The audio thread reads; nothing below allocates, locks or waits, except
// Tracktion's MIDI array, which keeps its storage from one block to the next
// and may grow once, the first time a note comes through.
class LiveInputPlugin final : public tracktion::Plugin
{
public:
    static const char* xmlTypeName;
    static const juce::Identifier trackProperty;

    // How the plugin reads the time of a message. The input clock in the
    // application; the Edit's own time in a render, for a test that plays
    // notes at known instants of a rendered song (a render runs faster than
    // the clock, and has no device to keep time).
    enum class Clock
    {
        input,
        edit
    };

    explicit LiveInputPlugin(tracktion::PluginCreationInfo info);
    ~LiveInputPlugin() override;

    LiveInputPlugin(const LiveInputPlugin&) = delete;
    LiveInputPlugin& operator=(const LiveInputPlugin&) = delete;
    LiveInputPlugin(LiveInputPlugin&&) = delete;
    LiveInputPlugin& operator=(LiveInputPlugin&&) = delete;

    [[nodiscard]] static juce::ValueTree create(const juce::String& track);
    [[nodiscard]] juce::String track() const;

    // Where its messages come from. Set by the projector on the message
    // thread; a plugin in a copy of the Edit (a render of the mix, a stem)
    // has none and does nothing.
    void bind(domain::live::TrackQueue* queue, domain::live::Router* router) noexcept
    {
        router_.store(router, std::memory_order_release);
        queue_.store(queue, std::memory_order_release);
    }
    [[nodiscard]] bool bound() const noexcept { return queue_.load(std::memory_order_acquire) != nullptr; }

    static void setClock(Clock clock) noexcept { clock_.store(clock, std::memory_order_relaxed); }
    [[nodiscard]] static Clock clock() noexcept { return clock_.load(std::memory_order_relaxed); }

    // How long a note waits between being played and being heard, beyond
    // the sound card's own latency: the regular timeline's delay at the last
    // block, in seconds. Zero before the first block.
    [[nodiscard]] double wait() const noexcept { return wait_.load(std::memory_order_relaxed); }

    // --- tracktion::Plugin
    juce::String getName() const override { return "DAW IA jeu"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getShortName(int) override { return "Jeu"; }
    juce::String getSelectableDescription() override { return "DAW IA jeu"; }
    bool canBeDisabled() override { return false; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    bool takesMidiInput() override { return true; }
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }

    void initialise(const tracktion::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void applyToBuffer(const tracktion::PluginRenderContext& context) override;

private:
    void keepHeldNotesSounding(tracktion::MidiMessageArray& midi) const;
    void remember(const domain::live::Event& event) noexcept;

    std::atomic<domain::live::TrackQueue*> queue_{nullptr};
    // Told where the song is, at each block, for a take (S23).
    std::atomic<domain::live::Router*> router_{nullptr};
    inline static std::atomic<Clock> clock_{Clock::input};

    // The audio thread's own.
    domain::live::Timeline timeline_;
    domain::live::Event next_{};
    bool hasNext_{false};
    std::array<std::bitset<128>, 16> held_{}; // per channel, the notes played live and not released
    std::bitset<16> pedal_{};                 // per channel, the sustain pedal played live, down
    double sampleRate_{44100.0};
    tracktion::MPESourceID source_;

    std::atomic<double> wait_{0.0};
};

} // namespace daw::engine
