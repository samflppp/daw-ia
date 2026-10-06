#include "daw/domain/live/TypingKeyboard.h"

#include <algorithm>

namespace daw::domain::live
{
namespace
{

// Scan codes, set 1, of the modifiers that make a letter a shortcut. Left
// and right Ctrl share 0x1D, left and right Alt 0x38 (the right ones come
// « extended »); the Windows keys are 0x5B and 0x5C, extended.
constexpr int ctrlKey = 0x1D;
constexpr int altKey = 0x38;
constexpr int leftWindows = 0x5B;
constexpr int rightWindows = 0x5C;

constexpr int ctrlBit = 1;
constexpr int altBit = 2;
constexpr int windowsBit = 4;

const std::vector<TypingKeyboard::Key> keys{
    // The bottom row, from the C of the octave.
    {0x2C, "W", "Z", 0},
    {0x1F, "S", "S", 1},
    {0x2D, "X", "X", 2},
    {0x20, "D", "D", 3},
    {0x2E, "C", "C", 4},
    {0x2F, "V", "V", 5},
    {0x22, "G", "G", 6},
    {0x30, "B", "B", 7},
    {0x23, "H", "H", 8},
    {0x31, "N", "N", 9},
    {0x24, "J", "J", 10},
    {0x32, ",", "M", 11},
    {0x33, ";", ",", 12},
    {0x26, "L", "L", 13},
    {0x34, ":", ".", 14},
    {0x27, "M", ";", 15},
    {0x35, "!", "/", 16},
    // The upper row, an octave higher.
    {0x10, "A", "Q", 12},
    {0x03, "é", "2", 13},
    {0x11, "Z", "W", 14},
    {0x04, "\"", "3", 15},
    {0x12, "E", "E", 16},
    {0x13, "R", "R", 17},
    {0x06, "(", "5", 18},
    {0x14, "T", "T", 19},
    {0x07, "-", "6", 20},
    {0x15, "Y", "Y", 21},
    {0x08, "è", "7", 22},
    {0x16, "U", "U", 23},
    {0x17, "I", "I", 24},
    {0x0A, "ç", "9", 25},
    {0x18, "O", "O", 26},
    {0x0B, "à", "0", 27},
    {0x19, "P", "P", 28},
    {0x1A, "^", "[", 29},
    {0x0D, "=", "=", 30},
    {0x1B, "$", "]", 31},
};

} // namespace

std::optional<int> TypingKeyboard::semitoneOf(int scanCode) noexcept
{
    for (const auto& key : keys)
    {
        if (key.scanCode == scanCode)
            return key.semitone;
    }
    return std::nullopt;
}

const std::vector<TypingKeyboard::Key>& TypingKeyboard::table()
{
    return keys;
}

TypingKeyboard::TypingKeyboard(Router& router)
    : router_(router)
{
    started_.fill(-1);
}

bool TypingKeyboard::listening() const noexcept
{
    return playing_.load(std::memory_order_relaxed) && !typing_.load(std::memory_order_relaxed) &&
           foreground_.load(std::memory_order_relaxed);
}

bool TypingKeyboard::key(int scanCode, bool extended, bool down, double seconds) noexcept
{
    if (scanCode < 0 || scanCode > 255)
        return false;
    const auto index = static_cast<std::size_t>(scanCode);

    // The modifiers, whatever the mode: a Ctrl held before the mode came on
    // still makes a letter a shortcut.
    int bit = 0;
    if (scanCode == ctrlKey)
        bit = ctrlBit;
    else if (scanCode == altKey)
        bit = altBit;
    else if (extended && (scanCode == leftWindows || scanCode == rightWindows))
        bit = windowsBit;
    if (bit != 0)
    {
        modifiers_ = down ? (modifiers_ | bit) : (modifiers_ & ~bit);
        return false;
    }

    const bool repeat = down && down_[index];
    down_[index] = down;

    if (!down)
    {
        // The note this key started, whatever the octave or the mode is now.
        const auto note = started_[index];
        started_[index] = -1;
        if (note < 0)
            return false;
        static_cast<void>(router_.noteOff(Router::computerKeyboard, 1, note, seconds));
        return true;
    }

    if (repeat || !listening() || modifiers_ != 0)
        return false;

    if (extended && (scanCode == leftArrow || scanCode == rightArrow))
    {
        setOctave(octave() + (scanCode == rightArrow ? 1 : -1));
        return true;
    }
    if (extended)
        return false; // the keypad's Enter, the arrows' block: not notes

    const auto semitone = semitoneOf(scanCode);
    if (!semitone)
        return false;
    const auto note = 12 * (octave() + 1) + *semitone;
    if (note < 0 || note > 127)
        return true;
    if (router_.noteOn(Router::computerKeyboard, 1, note, velocity(), seconds))
        started_[index] = note;
    return true;
}

void TypingKeyboard::releaseEverything() noexcept
{
    static_cast<void>(router_.release(Router::computerKeyboard, now()));
}

void TypingKeyboard::setPlaying(bool playing) noexcept
{
    if (playing_.exchange(playing, std::memory_order_relaxed) && !playing)
        releaseEverything();
}

void TypingKeyboard::setTyping(bool typing) noexcept
{
    if (!typing_.exchange(typing, std::memory_order_relaxed) && typing)
        releaseEverything();
}

void TypingKeyboard::setForeground(bool foreground) noexcept
{
    if (foreground_.exchange(foreground, std::memory_order_relaxed) && !foreground)
        releaseEverything();
}

void TypingKeyboard::setOctave(int octave) noexcept
{
    octave_.store(std::clamp(octave, lowestOctave, highestOctave), std::memory_order_relaxed);
}

void TypingKeyboard::setVelocity(int velocity) noexcept
{
    velocity_.store(std::clamp(velocity, 1, 127), std::memory_order_relaxed);
}

} // namespace daw::domain::live
