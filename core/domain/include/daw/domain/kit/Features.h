#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/mix/Measurement.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>

namespace daw::domain::kit
{

// The kit, « assembler » version (S24): a drum kit chosen from the person's
// own samples by what they measure, never by their names alone. Here, what a
// sample is measured to be: numbers, from its samples, by local
// deterministic code — the same octave bands as the mix and the direction.

// What a sample plays in a kit.
enum class Role
{
    kick,
    snare,
    clap,
    closedHat,
    openHat,
    percussion,
    bass808,
};

[[nodiscard]] std::string_view nameOf(Role role) noexcept; // « kick », « charley ouvert »…
[[nodiscard]] std::optional<Role> roleFromName(std::string_view name) noexcept; // the internal token

// The low end, from 20 to 160 Hz by sixths of an octave: where a kick and an
// 808 meet, or not.
inline constexpr std::size_t lowBands = 18;
inline constexpr double lowFromHz = 20.0;
[[nodiscard]] double lowBandCentre(std::size_t band) noexcept;

struct Features
{
    double seconds{0.0};       // the whole file
    double lengthSeconds{0.0}; // until it falls 60 dB under its peak for good
    double attackMs{0.0};      // from 10 % to 90 % of the peak, on a 1 ms envelope
    double tailSeconds{0.0};   // after the peak, from -10 dB to -40 dB under it
    double peakDb{mix::silenceDb};
    double rmsDb{mix::silenceDb}; // over its length
    double crestDb{0.0};          // the loudest 10 ms against the RMS, as the mix measures it
    double width{0.0};            // the side's share of the energy, 0 mono to 0.5 unrelated
    std::array<double, mix::bandCount> bandsDb{};
    double centroidHz{0.0};
    double highShare{0.0}; // the share of the energy above 5 kHz

    // The pitch, when it has one (YIN every 10 ms): the median of its body,
    // where the pitch holds within 10 cents a hop and the level within 30 dB
    // of the peak; how far from the nearest note, in cents; and the glide
    // into it, in semitones, from the first pitch heard. YIN reads 85 ms at a
    // time (two periods of 30 Hz): a glide shorter than that reads smaller
    // than it is. Under 100 Hz YIN may take the octave above or below: the
    // class is what is kept.
    bool pitched{false};
    double pitchHz{0.0};
    int pitchClass{-1}; // 0 is C
    double cents{0.0};
    double glideSemitones{0.0};

    std::array<double, lowBands> lowProfileDb{};
    double lowPeakHz{0.0}; // the loudest band of the low profile

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Features> fromValue(const Value& value);
};

// Measures one sample. `right` may be `left` for mono.
[[nodiscard]] Features measure(const float* left, const float* right, std::size_t count, double sampleRate);

// The role a sample plays: what its name and its folders say (the words a
// sample pack uses), confirmed or refused by what it measures — an open hat
// lasts more than 250 ms, a closed one less; an 808 has a pitch; a kick and
// an 808 live under 2 kHz, a hat above. Nothing when the name says nothing,
// or when the sound refuses it.
[[nodiscard]] std::optional<Role> roleOf(std::string_view path, const Features& features);

} // namespace daw::domain::kit
