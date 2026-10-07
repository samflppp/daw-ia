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
    // Told every key as well, on the keyboard's thread, after the
    // TypingKeyboard: the push-to-talk (S25) reads its key there.
    struct Listener
    {
        virtual ~Listener() = default;
        // `inFront`: whether one of this process's windows was in front when
        // the key went down (a key-up always comes, wherever it happens).
        virtual void keyEvent(int scanCode, bool extended, bool down, bool inFront, double seconds) = 0;
    };

    explicit RawKeyboard(domain::live::TypingKeyboard& keys);
    ~RawKeyboard() override;

    RawKeyboard(const RawKeyboard&) = delete;
    RawKeyboard& operator=(const RawKeyboard&) = delete;
    RawKeyboard(RawKeyboard&&) = delete;
    RawKeyboard& operator=(RawKeyboard&&) = delete;

    // Whether the keyboard is being read: Raw Input registered and its
    // thread alive.
    [[nodiscard]] bool running() const noexcept { return registered_.load(std::memory_order_acquire); }

    // One listener at most; null removes it. Any thread.
    void setListener(Listener* listener) noexcept { listener_.store(listener, std::memory_order_release); }
    [[nodiscard]] Listener* listener() const noexcept { return listener_.load(std::memory_order_acquire); }
    [[nodiscard]] domain::live::TypingKeyboard& keys() noexcept { return keys_; }

private:
    void run() override;

    domain::live::TypingKeyboard& keys_;
    std::atomic<bool> registered_{false};
    std::atomic<void*> window_{nullptr};
    std::atomic<Listener*> listener_{nullptr};
};

} // namespace daw::app
