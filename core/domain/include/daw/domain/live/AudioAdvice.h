#pragma once

#include "daw/domain/Value.h"

#include <cstdint>
#include <string>
#include <vector>

namespace daw::domain::live
{

// The « Audio » window's advice (S24): which driver and which buffer to play
// with, measured on this machine's sound card — never read from the names.
//
// Decided on 6 October 2026, after measuring the founder's card: a Realtek in
// exclusive mode stalls about fifteen times a second, up to 75 ms, whatever
// the buffer; a note played live waited 53.8 ms there against 12.5 ms in the
// shared mode. A rule by driver name would have advised the worst setup. So
// « Tester ma carte » tries each setup for a few seconds, and the advice is
// the setup where a note waits least at worst, from the key to the ear,
// among those that never dropped a block — 256 samples or less preferred.
//
// What JUCE offers under Windows, by the names it gives them:
//   - « Windows Audio »: WASAPI shared, the default of Windows. One buffer
//     only, the one Windows imposes (480 samples, 10 ms, at 48 kHz).
//   - « Windows Audio (Low Latency Mode) »: shared too, other applications
//     keep playing, in multiples of the driver's period down to its minimum
//     — when the driver allows it.
//   - « Windows Audio (Exclusive Mode) »: the card for this application
//     alone; the others fall silent while it plays.
// ASIO is not compiled in (the SDK and its licence, after S26), and
// DirectSound is never offered: its latency is higher than any of these.
//
// A machine setting, never the project's. Pure: the lists come from the
// engine, the advice is the same for the same lists.
inline constexpr std::string_view sharedDriver = "Windows Audio";
inline constexpr std::string_view lowLatencyDriver = "Windows Audio (Low Latency Mode)";
inline constexpr std::string_view exclusiveDriver = "Windows Audio (Exclusive Mode)";

// At most this many samples: 5.3 ms at 48 kHz, a wait the hand does not feel.
// A preference among the setups that hold, never a reason to take one that
// drops out.
inline constexpr int advisedMaxBuffer = 256;

// The exclusive buffers tried, when the card offers them.
inline constexpr int exclusiveTrials[] = {144, 192, 256, 480};

// A trial counts with at least this many blocks measured.
inline constexpr std::int64_t trialMinBlocks = 100;

// One driver, as the chosen output offers it.
struct AudioDriver
{
    std::string type;
    std::vector<int> buffers; // the sizes JUCE offers, in samples
};

struct AudioAdvice
{
    std::string type;           // the driver advised
    int buffer{0};              // its buffer, in samples
    bool silencesOthers{false}; // the exclusive mode
    bool already{false};        // what the card already plays with
    bool measured{false};       // drawn from a trial of this card
    std::string sentence;       // French, for the window
};

// The setups a trial opens, in order: the shared driver's one buffer, every
// Low Latency buffer up to 480 samples, the exclusive buffers above that the
// card offers (its smallest when it offers none of them).
struct TrialSetup
{
    std::string type;
    int buffer{0};
};
[[nodiscard]] std::vector<TrialSetup> trialSetups(const std::vector<AudioDriver>& drivers);

// The buffers a driver offers, the closest to `wanted`: what a window keeps
// when the driver changes under it. 0 when it offers none.
[[nodiscard]] int closestBuffer(const std::vector<int>& buffers, int wanted);

// A block that came this late after the one before it is a dropout: the card
// asked for samples and waited. Half a block of slack covers the jitter of
// the callback, which WASAPI does not keep to the microsecond.
[[nodiscard]] constexpr bool isLate(double intervalSeconds, double blockSeconds) noexcept
{
    return blockSeconds > 0.0 && intervalSeconds > 1.5 * blockSeconds;
}

// How the blocks really came, as the window shows them: measured on the
// audio callback, not declared by the card.
struct BlockTiming
{
    std::int64_t blocks{0};  // since the last reset
    int lastSize{0};         // samples in the last block: the buffer the card plays
    double meanSeconds{0.0}; // between two blocks
    double worstSeconds{0.0};
    std::int64_t late{0}; // dropouts, see isLate
};

// One setup tried on the card: what it opened, how its blocks came, what
// the card declared.
struct CardTrial
{
    std::string type;
    int buffer{0}; // what the card opened, which may differ from what was asked
    double sampleRate{0.0};
    BlockTiming timing;
    double outputSeconds{0.0};
};

// Held: enough blocks measured, and not one late.
[[nodiscard]] bool held(const CardTrial& trial) noexcept;

// At worst, from the key to the ear: the live input's regular wait (a block
// and its margin, see Timeline), what the slowest block came later than
// that margin allows, and what the card declares.
[[nodiscard]] double worstKeyToEar(const CardTrial& trial) noexcept;

// The setup where a note waits least at worst among those that held, at
// 256 samples or less when one of those held; nothing advised when none
// held, and nothing before a trial. Said in French, with its numbers.
[[nodiscard]] AudioAdvice
advise(const std::vector<CardTrial>& trials, const std::string& currentType, int currentBuffer);

// A card's trials, kept on the machine between sessions.
[[nodiscard]] Value toValue(const std::vector<CardTrial>& trials);
[[nodiscard]] std::vector<CardTrial> trialsFromValue(const Value& value);

// « 22,5 ms de la touche à l'oreille » and the rest, in French: the window's
// lines. `playSeconds` is what the live input measured, from the key to the
// first sample rendered; `outputSeconds` what the card declares.
[[nodiscard]] std::string describeLatency(double playSeconds, bool measured, double outputSeconds);

} // namespace daw::domain::live
