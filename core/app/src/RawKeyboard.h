#pragma once

#include "daw/domain/live/TypingKeyboard.h"

#include <juce_core/juce_core.h>

#include <atomic>

namespace daw::app
{

// The computer's keyboard read by the place of each key (S23, decided on 6
// October 2026): Raw Input on a thread of its own, with a window that only
// receives messages. Each key's scan code goes to the TypingKeyboard the
// moment Windows hands it over — the message thread, busy with a repaint,
// is never in the way. Registered with RIDEV_INPUTSINK, so a key-up that
// happens after the window went behind still comes; a key-down only plays
// when one of this process's windows is in front.
//
// Windows only. Elsewhere it does not run, and the mode cannot be turned on.
class RawKeyboard final : private juce::Thread
{
public:
    explicit RawKeyboard(domain::live::TypingKeyboard& keys);
    ~RawKeyboard() override;

    RawKeyboard(const RawKeyboard&) = delete;
    RawKeyboard& operator=(const RawKeyboard&) = delete;
    RawKeyboard(RawKeyboard&&) = delete;
    RawKeyboard& operator=(RawKeyboard&&) = delete;

    // Whether the keyboard is being read: Raw Input registered and its
    // thread alive.
    [[nodiscard]] bool running() const noexcept { return registered_.load(std::memory_order_acquire); }

private:
    void run() override;

    domain::live::TypingKeyboard& keys_;
    std::atomic<bool> registered_{false};
    std::atomic<void*> window_{nullptr};
};

} // namespace daw::app
