#pragma once

#include "daw/domain/Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace daw::domain
{

struct PluginInstance;

// The effects the DAW ships, as opposed to the plugins the user installed (S20).
//
// A third-party plugin is opaque: its parameters are whatever the plugin calls
// them, on a 0..1 scale only the plugin can translate. These are not. Their
// parameters are named and bounded here, in the units a mixing engineer reads —
// hertz, decibels, a ratio, milliseconds — because that is what lets a copilot
// write "j'ai creusé la basse de 3 dB à 60 Hz" and mean it, and what lets a
// beginner read the same number on the screen.
//
// They enter the project as a PluginInstance whose ref.format is "internal" and
// whose ref.identifier is one of the constants below: no new verb, the five
// plugin commands carry them as they carry a VST3. What changes is the scale of
// a parameter's value, which the instance's format decides. Nothing of them is a
// blob: their whole state is their parameters, so a project made of them
// replays to the byte.
//
// Sparse like any plugin: a parameter absent from the instance has the default
// written here. The engine projects one onto the effects Tracktion ships; the
// mapping lives in engine/ProjectProjector, the meaning lives here.
struct InternalParameter
{
    std::string_view id;
    std::string_view label; // French, for the screen
    std::string_view unit;  // "Hz", "dB", ":1", "ms", "Q"
    double minimum{0.0};
    double maximum{1.0};
    double defaultValue{0.0};
};

struct InternalEffect
{
    std::string_view identifier;
    std::string_view name; // French, for the screen
    std::vector<InternalParameter> parameters;

    [[nodiscard]] const InternalParameter* find(std::string_view parameterId) const noexcept;
};

namespace internal
{
inline constexpr std::string_view equaliser = "daw.eq";
inline constexpr std::string_view compressor = "daw.compressor";

// Equaliser: a high-pass, a low shelf, two bells, a high shelf.
inline constexpr std::string_view highPassFrequency = "hp_freq"; // 20 Hz: off
inline constexpr std::string_view lowFrequency = "low_freq";
inline constexpr std::string_view lowGain = "low_gain";
inline constexpr std::string_view lowQ = "low_q";
inline constexpr std::string_view mid1Frequency = "mid1_freq";
inline constexpr std::string_view mid1Gain = "mid1_gain";
inline constexpr std::string_view mid1Q = "mid1_q";
inline constexpr std::string_view mid2Frequency = "mid2_freq";
inline constexpr std::string_view mid2Gain = "mid2_gain";
inline constexpr std::string_view mid2Q = "mid2_q";
inline constexpr std::string_view highFrequency = "high_freq";
inline constexpr std::string_view highGain = "high_gain";
inline constexpr std::string_view highQ = "high_q";

// The high-pass is off at its lowest frequency: the effect then leaves the
// band below untouched, which is what "no high-pass" means.
inline constexpr double highPassOff = 20.0;

// Compressor.
inline constexpr std::string_view threshold = "threshold_db";
inline constexpr std::string_view ratio = "ratio";
inline constexpr std::string_view attack = "attack_ms";
inline constexpr std::string_view release = "release_ms";
inline constexpr std::string_view makeup = "makeup_db";
} // namespace internal

// Every internal effect, in a fixed order.
[[nodiscard]] const std::vector<InternalEffect>& internalEffects();

// The effect an identifier names, or nullptr.
[[nodiscard]] const InternalEffect* findInternalEffect(std::string_view identifier) noexcept;

// The value an internal instance plays with for one of its parameters: the one
// it holds, or the default. Zero for a parameter the effect does not have.
[[nodiscard]] double internalValue(const PluginInstance& instance, std::string_view parameterId);

// A parameter value an internal effect accepts: known identifier, finite, in
// bounds. Said in French when refused, because a copilot reads it.
[[nodiscard]] Result<void>
validateInternalParameter(const InternalEffect& effect, std::string_view parameterId, double value);

// What the equaliser does to a sine at `frequency`, in dB, as the engine plays
// it at `sampleRate`: the same biquads, the same formulas, computed here so the
// screen draws the curve and the mixing rules reason on it without the engine.
// An instance that is not an equaliser, or is bypassed, gives 0.
[[nodiscard]] double
equaliserGainDb(const PluginInstance& instance, double frequency, double sampleRate = 48000.0);

} // namespace daw::domain
