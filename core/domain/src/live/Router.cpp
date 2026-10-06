#include "daw/domain/live/Router.h"

#include <chrono>

namespace daw::domain::live
{
namespace
{

constexpr std::uint8_t noteOnStatus = 0x90;
constexpr std::uint8_t noteOffStatus = 0x80;
constexpr std::uint8_t controllerStatus = 0xB0;
constexpr std::uint8_t pitchBendStatus = 0xE0;
constexpr std::uint8_t releaseVelocity = 64;
constexpr int sustainController = 64;
constexpr int modulationController = 1;
constexpr int bendCentre = 8192;

} // namespace

double now() noexcept
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

Router::Router() = default;
Router::~Router() = default;

std::size_t Router::heldIndex(int source, int channel, int note) noexcept
{
    return (static_cast<std::size_t>(source) * 16 + static_cast<std::size_t>(channel - 1)) * 128 +
           static_cast<std::size_t>(note);
}

bool Router::valid(int source, int channel) noexcept
{
    return source >= 0 && source < sourceCount && channel >= 1 && channel <= 16;
}

TrackQueue* Router::queueFor(const std::string& track)
{
    if (track.empty())
        return nullptr;

    const auto count = slotCount_.load(std::memory_order_relaxed);
    for (int index = 0; index < count; ++index)
    {
        auto* slot = slots_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
        if (slot != nullptr && slot->track == track)
            return &slot->queue;
    }
    if (count >= maxTracks)
        return nullptr;

    auto made = std::make_unique<Slot>();
    made->track = track;
    auto* slot = made.get();
    owned_.push_back(std::move(made));
    slots_[static_cast<std::size_t>(count)].store(slot, std::memory_order_release);
    slotCount_.store(count + 1, std::memory_order_release);
    return &slot->queue;
}

void Router::setTarget(const std::string& track, double seconds)
{
    int wanted = -1;
    if (!track.empty() && queueFor(track) != nullptr)
    {
        const auto count = slotCount_.load(std::memory_order_relaxed);
        for (int index = 0; index < count; ++index)
        {
            if (slots_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed)->track == track)
                wanted = index;
        }
    }

    const auto before = target_.exchange(wanted, std::memory_order_acq_rel);
    if (before < 0 || before == wanted)
        return;

    // The track left keeps its notes and its pedal, whose releases follow
    // them; the wheels it was given come back to rest.
    const auto at = seconds;
    for (int channel = 1; channel <= 16; ++channel)
    {
        const auto status = static_cast<std::uint8_t>(channel - 1);
        static_cast<void>(send(before,
                               static_cast<std::uint8_t>(pitchBendStatus | status),
                               static_cast<std::uint8_t>(bendCentre & 0x7F),
                               static_cast<std::uint8_t>(bendCentre >> 7),
                               at));
        static_cast<void>(send(before,
                               static_cast<std::uint8_t>(controllerStatus | status),
                               static_cast<std::uint8_t>(modulationController),
                               0,
                               at));
    }
}

std::string Router::target() const
{
    const auto index = target_.load(std::memory_order_acquire);
    if (index < 0)
        return {};
    const auto* slot = slots_[static_cast<std::size_t>(index)].load(std::memory_order_acquire);
    return slot != nullptr ? slot->track : std::string{};
}

bool Router::send(
    int slot, std::uint8_t status, std::uint8_t first, std::uint8_t second, double seconds) noexcept
{
    if (slot < 0 || slot >= maxTracks)
        return false;
    auto* target = slots_[static_cast<std::size_t>(slot)].load(std::memory_order_acquire);
    if (target == nullptr)
        return false;

    Event event;
    event.bytes = {status, first, second};
    event.size = 3;
    event.seconds = seconds;
    if (!target->queue.push(event))
    {
        refused_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // A take keeps the notes and the pedal, nothing else: the modulation and
    // the bend are played, not written (S23).
    const auto kind = status & 0xF0;
    if (recording_.load(std::memory_order_acquire) &&
        (kind == noteOnStatus || kind == noteOffStatus ||
         (kind == controllerStatus && first == sustainController)))
    {
        event.slot = static_cast<std::int16_t>(slot);
        if (!take_.push(event))
            refused_.fetch_add(1, std::memory_order_relaxed);
    }
    return true;
}

std::string Router::trackOf(int slot) const
{
    if (slot < 0 || slot >= slotCount_.load(std::memory_order_acquire))
        return {};
    const auto* found = slots_[static_cast<std::size_t>(slot)].load(std::memory_order_acquire);
    return found != nullptr ? found->track : std::string{};
}

void Router::publish(const Position& position) noexcept
{
    auto sequence = sequence_.load(std::memory_order_relaxed);
    if ((sequence & 1U) != 0 ||
        !sequence_.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acquire))
        return;
    clock_.store(position.clock, std::memory_order_relaxed);
    editSeconds_.store(position.editSeconds, std::memory_order_relaxed);
    playing_.store(position.playing, std::memory_order_relaxed);
    sequence_.store(sequence + 2, std::memory_order_release);
}

bool Router::position(Position& position) const noexcept
{
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const auto before = sequence_.load(std::memory_order_acquire);
        if (before == 0)
            return false; // nothing published yet
        if ((before & 1U) != 0)
            continue;
        position.clock = clock_.load(std::memory_order_relaxed);
        position.editSeconds = editSeconds_.load(std::memory_order_relaxed);
        position.playing = playing_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (sequence_.load(std::memory_order_relaxed) == before)
            return true;
    }
    return false;
}

bool Router::noteOn(int source, int channel, int note, int velocity, double seconds) noexcept
{
    if (!valid(source, channel) || note < 0 || note > 127 || velocity < 1 || velocity > 127)
        return false;
    const auto slot = target_.load(std::memory_order_acquire);
    if (slot < 0)
        return false;

    // A note-on for a key already down (a release that never came) closes
    // the first one where it went, before the second starts.
    const auto status = static_cast<std::uint8_t>(channel - 1);
    const auto before = held_[heldIndex(source, channel, note)].exchange(static_cast<std::int16_t>(slot + 1),
                                                                         std::memory_order_acq_rel);
    if (before != 0)
        static_cast<void>(send(before - 1,
                               static_cast<std::uint8_t>(noteOffStatus | status),
                               static_cast<std::uint8_t>(note),
                               releaseVelocity,
                               seconds));

    if (send(slot,
             static_cast<std::uint8_t>(noteOnStatus | status),
             static_cast<std::uint8_t>(note),
             static_cast<std::uint8_t>(velocity),
             seconds))
        return true;

    // Refused: it is not held either, or its release would go nowhere.
    auto expected = static_cast<std::int16_t>(slot + 1);
    held_[heldIndex(source, channel, note)].compare_exchange_strong(expected, 0, std::memory_order_acq_rel);
    return false;
}

bool Router::noteOff(int source, int channel, int note, double seconds) noexcept
{
    if (!valid(source, channel) || note < 0 || note > 127)
        return false;
    const auto before = held_[heldIndex(source, channel, note)].exchange(0, std::memory_order_acq_rel);
    if (before == 0)
        return false;
    return send(before - 1,
                static_cast<std::uint8_t>(noteOffStatus | static_cast<std::uint8_t>(channel - 1)),
                static_cast<std::uint8_t>(note),
                releaseVelocity,
                seconds);
}

bool Router::controller(int source, int channel, int number, int value, double seconds) noexcept
{
    if (!valid(source, channel) || number < 0 || number > 127 || value < 0 || value > 127)
        return false;
    const auto status = static_cast<std::uint8_t>(controllerStatus | static_cast<std::uint8_t>(channel - 1));
    auto& pedal = pedal_[static_cast<std::size_t>(source) * 16 + static_cast<std::size_t>(channel - 1)];

    if (number == sustainController)
    {
        if (value >= 64)
        {
            const auto slot = target_.load(std::memory_order_acquire);
            if (slot < 0)
                return false;
            const auto before =
                pedal.exchange(static_cast<std::int16_t>(slot + 1), std::memory_order_acq_rel);
            if (before != 0 && before != slot + 1)
                static_cast<void>(send(before - 1, status, sustainController, 0, seconds));
            return send(slot, status, sustainController, static_cast<std::uint8_t>(value), seconds);
        }
        const auto before = pedal.exchange(0, std::memory_order_acq_rel);
        return before != 0 &&
               send(before - 1, status, sustainController, static_cast<std::uint8_t>(value), seconds);
    }

    const auto slot = target_.load(std::memory_order_acquire);
    return slot >= 0 &&
           send(slot, status, static_cast<std::uint8_t>(number), static_cast<std::uint8_t>(value), seconds);
}

bool Router::pitchBend(int source, int channel, int value, double seconds) noexcept
{
    if (!valid(source, channel) || value < 0 || value > 16383)
        return false;
    const auto slot = target_.load(std::memory_order_acquire);
    return slot >= 0 &&
           send(slot,
                static_cast<std::uint8_t>(pitchBendStatus | static_cast<std::uint8_t>(channel - 1)),
                static_cast<std::uint8_t>(value & 0x7F),
                static_cast<std::uint8_t>(value >> 7),
                seconds);
}

int Router::release(int source, double seconds) noexcept
{
    if (source < 0 || source >= sourceCount)
        return 0;
    int released = 0;
    for (int channel = 1; channel <= 16; ++channel)
    {
        const auto status = static_cast<std::uint8_t>(channel - 1);
        for (int note = 0; note < 128; ++note)
        {
            const auto before =
                held_[heldIndex(source, channel, note)].exchange(0, std::memory_order_acq_rel);
            if (before == 0)
                continue;
            static_cast<void>(send(before - 1,
                                   static_cast<std::uint8_t>(noteOffStatus | status),
                                   static_cast<std::uint8_t>(note),
                                   releaseVelocity,
                                   seconds));
            ++released;
        }
        const auto pedal =
            pedal_[static_cast<std::size_t>(source) * 16 + static_cast<std::size_t>(channel - 1)].exchange(
                0, std::memory_order_acq_rel);
        if (pedal != 0)
            static_cast<void>(send(pedal - 1,
                                   static_cast<std::uint8_t>(controllerStatus | status),
                                   sustainController,
                                   0,
                                   seconds));
    }
    return released;
}

int Router::releaseAll(double seconds) noexcept
{
    int released = 0;
    for (int source = 0; source < sourceCount; ++source)
        released += release(source, seconds);
    return released;
}

void Router::dropQueued() noexcept
{
    const auto count = slotCount_.load(std::memory_order_acquire);
    Event dropped;
    for (int index = 0; index < count; ++index)
    {
        auto* slot = slots_[static_cast<std::size_t>(index)].load(std::memory_order_acquire);
        if (slot == nullptr)
            continue;
        while (slot->queue.pop(dropped))
        {
        }
    }
}

void Router::silence(double seconds) noexcept
{
    dropQueued();
    static_cast<void>(releaseAll(seconds));
}

int Router::held() const noexcept
{
    int count = 0;
    for (const auto& note : held_)
        count += note.load(std::memory_order_relaxed) != 0 ? 1 : 0;
    return count;
}

} // namespace daw::domain::live
