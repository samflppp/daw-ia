#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace daw::domain::live
{

// Playing live (S23): a key of a MIDI keyboard or of the computer's keyboard
// plays the instrument of the chosen track, now, through the engine. It
// writes nothing into the project, sends no command, and never waits for the
// message thread. This file is the part of that path with a right answer —
// where a note goes, where its release goes, what is left sounding — kept
// free of JUCE and of Tracktion so that a test can pin it down.
//
// The threads:
//   - an input thread per source (the MIDI callback of a device, the thread
//     of the computer's keyboard) calls noteOn / noteOff / controller /
//     pitchBend: it reads the target, writes the table of held notes, and
//     pushes the message into the queue of that track;
//   - the audio thread, in the engine's plugin at the head of the track's
//     chain, pops the queue and hands the messages to the instrument;
//   - the message thread chooses the target and makes the queues, and
//     nothing else; it can also release everything (a window that loses its
//     focus, a sound card that goes away).
// No call here allocates, locks or waits; queueFor, the only one that
// allocates, is the message thread's.

// One MIDI message of up to three bytes, and when it was played, on the
// input clock (seconds, monotonic: see now()).
struct Event
{
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size{0};
    double seconds{0.0};
};

// The input clock: a monotonic count of seconds, the same on every thread.
// On Windows it is the performance counter.
[[nodiscard]] double now() noexcept;

// A bounded queue, many writers and many readers, without a lock (Dmitry
// Vyukov's): each cell carries a sequence number that says whether it is free
// to write or ready to read. A full queue refuses rather than waits.
template <std::size_t Capacity>
class Queue
{
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "a power of two");

public:
    Queue() noexcept
    {
        for (std::size_t index = 0; index < Capacity; ++index)
            cells_[index].sequence.store(index, std::memory_order_relaxed);
    }

    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;
    ~Queue() = default;

    [[nodiscard]] bool push(const Event& event) noexcept
    {
        auto position = enqueue_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto& cell = cells_[position & mask];
            const auto sequence = cell.sequence.load(std::memory_order_acquire);
            const auto difference =
                static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position);
            if (difference == 0)
            {
                if (enqueue_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    cell.event = event;
                    cell.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false; // full
            }
            else
            {
                position = enqueue_.load(std::memory_order_relaxed);
            }
        }
    }

    [[nodiscard]] bool pop(Event& event) noexcept
    {
        auto position = dequeue_.load(std::memory_order_relaxed);
        for (;;)
        {
            auto& cell = cells_[position & mask];
            const auto sequence = cell.sequence.load(std::memory_order_acquire);
            const auto difference =
                static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(position + 1);
            if (difference == 0)
            {
                if (dequeue_.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    event = cell.event;
                    cell.sequence.store(position + mask + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (difference < 0)
            {
                return false; // empty
            }
            else
            {
                position = dequeue_.load(std::memory_order_relaxed);
            }
        }
    }

private:
    static constexpr std::size_t mask = Capacity - 1;

    struct Cell
    {
        std::atomic<std::size_t> sequence{0};
        Event event{};
    };

    std::array<Cell, Capacity> cells_{};
    alignas(64) std::atomic<std::size_t> enqueue_{0};
    alignas(64) std::atomic<std::size_t> dequeue_{0};
};

// About a second of a ten-finger trill with its releases: more than a hand
// can play between two blocks.
using TrackQueue = Queue<512>;

class Router
{
public:
    // Who plays: the computer's keyboard, up to six MIDI inputs, and the
    // simulated input of the tests and of --verify-jeu. One thread per source.
    static constexpr int sourceCount = 8;
    static constexpr int computerKeyboard = 0;
    static constexpr int firstMidiInput = 1;
    static constexpr int midiInputCount = 6;
    static constexpr int simulation = 7;

    // The tracks a session can play, made on demand and kept until the end:
    // a queue the audio thread may still read is never freed under it.
    static constexpr int maxTracks = 512;

    Router();
    ~Router();

    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
    Router(Router&&) = delete;
    Router& operator=(Router&&) = delete;

    // --- the message thread

    // The queue of a track, by its identifier, made the first time. Null when
    // the session has made maxTracks already.
    [[nodiscard]] TrackQueue* queueFor(const std::string& track);

    // Which track the keys play: the channel chosen in the rack. Empty: none,
    // and a key plays nothing (noteOn says so). A change resets the pitch
    // bend and the modulation of the track left, at `seconds` on the clock
    // the messages are placed on; its held notes and its pedal stay where
    // they went, and their releases follow them.
    void setTarget(const std::string& track, double seconds = now());
    [[nodiscard]] std::string target() const;

    // --- an input thread, the one of `source`. Channels 1 to 16.

    // False when nothing plays it: no track chosen, a full queue, or a value
    // out of range.
    bool noteOn(int source, int channel, int note, int velocity, double seconds) noexcept;

    // The release goes to the track the note went to, whichever is chosen
    // now. False when the note was not held.
    bool noteOff(int source, int channel, int note, double seconds) noexcept;

    // Sustain (64) follows the pedal: up goes where down went. Any other
    // controller goes to the chosen track.
    bool controller(int source, int channel, int number, int value, double seconds) noexcept;
    bool pitchBend(int source, int channel, int value, double seconds) noexcept;

    // --- any thread

    // Every note `source` holds, released, and its pedal up: a keyboard that
    // is unplugged, a window that loses the computer's keyboard. Returns how
    // many notes were released.
    int release(int source, double seconds) noexcept;
    int releaseAll(double seconds) noexcept;

    // The sound card went away: what waits in the queues is dropped — it
    // would come out late, all at once —, then every held note is released
    // and every pedal lifted, so that the instruments are silent when a card
    // opens again.
    void silence(double seconds) noexcept;

    // How many notes are held now, over every source.
    [[nodiscard]] int held() const noexcept;

    // Messages a full queue refused since the start.
    [[nodiscard]] std::uint64_t refused() const noexcept { return refused_.load(std::memory_order_relaxed); }

private:
    struct Slot
    {
        std::string track;
        TrackQueue queue;
    };

    [[nodiscard]] static std::size_t heldIndex(int source, int channel, int note) noexcept;
    [[nodiscard]] static bool valid(int source, int channel) noexcept;
    bool
    send(int slot, std::uint8_t status, std::uint8_t first, std::uint8_t second, double seconds) noexcept;
    void dropQueued() noexcept;

    // Written by the message thread before a slot is published through
    // target_ or a held note; read by the others after.
    std::array<std::atomic<Slot*>, maxTracks> slots_{};
    std::vector<std::unique_ptr<Slot>> owned_;
    std::atomic<int> slotCount_{0};

    std::atomic<int> target_{-1};

    // Per source, channel and note: the slot the note went to, plus one; 0
    // when it is not held. Per source and channel, the same for the pedal.
    std::array<std::atomic<std::int16_t>, static_cast<std::size_t>(sourceCount) * 16 * 128> held_{};
    std::array<std::atomic<std::int16_t>, static_cast<std::size_t>(sourceCount) * 16> pedal_{};

    std::atomic<std::uint64_t> refused_{0};
};

} // namespace daw::domain::live
