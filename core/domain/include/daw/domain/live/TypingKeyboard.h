#pragma once

#include "daw/domain/live/Router.h"

#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace daw::domain::live
{

// The computer's keyboard played like a piano (S23), as in FL Studio.
//
// A key is read by its place on the keyboard — its scan code, set 1, the
// number the keyboard itself sends — never by the character it types: a
// QWERTY keyboard plays the same notes at the same places as an AZERTY one,
// and a dead key (^ on AZERTY) plays like any other.
//
// Two rows, two octaves, the layout of FL. The bottom row from W (AZERTY;
// Z on QWERTY) is a C, its black keys on the row above (S D, G H J); the row
// of A (AZERTY; Q on QWERTY) is the C an octave higher, its black keys on the
// digits. Each row runs on past its octave, as in FL.
//
// When it plays: in the mode, turned on by the person (a button of the
// transport, Ctrl+T); never while a text field has the focus, never while
// the window is in the background, never while Ctrl, Alt or the Windows key
// is held — a letter with Ctrl is a shortcut, always. ← and → change the
// octave, in the mode only; F and K are not notes, and keep their meaning.
//
// Threads: key() is the keyboard thread's (Raw Input on Windows, or a test);
// the setters may be called from any thread. Releasing goes through the
// router, which is safe from any thread.
class TypingKeyboard
{
public:
    // The scan codes of ← and →, which come with the « extended » flag.
    static constexpr int leftArrow = 0x4B;
    static constexpr int rightArrow = 0x4D;

    static constexpr int lowestOctave = 0;
    static constexpr int highestOctave = 7; // the top key of the upper row is then MIDI 127
    static constexpr int defaultOctave = 4; // the bottom row starts on MIDI 60, do3 in French naming
    static constexpr int defaultVelocity = 100;

    // The semitone a key plays above the C of the bottom row, by scan code;
    // nothing for a key that is not a note.
    [[nodiscard]] static std::optional<int> semitoneOf(int scanCode) noexcept;

    // The table, for the documentation and the screen: each note key, by its
    // scan code, what it is labelled on an AZERTY and on a QWERTY keyboard,
    // and the semitone it plays.
    struct Key
    {
        int scanCode;
        const char* azerty;
        const char* qwerty;
        int semitone;
    };
    [[nodiscard]] static const std::vector<Key>& table();

    explicit TypingKeyboard(Router& router);

    // --- the keyboard thread

    // A key went down or came up. Auto-repeat (a down for a key already
    // down) plays nothing. Returns whether it was taken as a note or an
    // octave change — a key that was, the window's shortcuts must not see.
    bool key(int scanCode, bool extended, bool down, double seconds) noexcept;

    // --- any thread

    void setPlaying(bool playing) noexcept;
    [[nodiscard]] bool playing() const noexcept { return playing_.load(std::memory_order_relaxed); }

    // A text field has the focus: the keys type, nothing plays.
    void setTyping(bool typing) noexcept;
    [[nodiscard]] bool typing() const noexcept { return typing_.load(std::memory_order_relaxed); }

    // The window is in front. When it goes behind, what the keys hold is
    // released: their key-ups may go to another window.
    void setForeground(bool foreground) noexcept;

    void setOctave(int octave) noexcept;
    [[nodiscard]] int octave() const noexcept { return octave_.load(std::memory_order_relaxed); }

    void setVelocity(int velocity) noexcept;
    [[nodiscard]] int velocity() const noexcept { return velocity_.load(std::memory_order_relaxed); }

    // Whether a key, at this moment, would play.
    [[nodiscard]] bool listening() const noexcept;

private:
    void releaseEverything() noexcept;

    Router& router_;
    std::atomic<bool> playing_{false};
    std::atomic<bool> typing_{false};
    std::atomic<bool> foreground_{true};
    std::atomic<int> octave_{defaultOctave};
    std::atomic<int> velocity_{defaultVelocity};

    // The keyboard thread's own: which keys are down, the note each one
    // started (its release must end that note, whatever the octave is now),
    // and the modifiers held.
    std::array<bool, 256> down_{};
    std::array<int, 256> started_{};
    int modifiers_{0};
};

} // namespace daw::domain::live
