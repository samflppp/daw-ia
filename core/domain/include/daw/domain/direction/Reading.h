#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/mix/Measurement.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace daw::domain::direction
{

// What a reference recording says, in numbers (S22). Never its audio, never
// its notes: a tempo, a key, where its sections are, who plays when, how loud
// each stem stands against the others, the shape of its spectrum, its
// dynamics, its width. Read from its four stems by local, deterministic code
// — measuring is not deciding (S20) —, each value with how far it can be
// trusted. A value that cannot be trusted is left out: a key guessed wrong is
// worse than no key.

// The stems a reference is read from, the separator's names.
inline constexpr std::array<const char*, 4> stemNames{"vocals", "drums", "bass", "other"};

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

struct Section
{
    double fromSeconds{0.0};
    double toSeconds{0.0};
    char label{'A'};        // A, B, C…: the same letter for sections that sound alike
    double loudnessDb{0.0}; // mean level of the mix there, dBFS
    std::array<double, 4>
        activity{}; // per stem, in stemNames' order: the share of the section where it plays
};

struct StemReading
{
    double loudnessLufs{mix::silenceDb};
    double balanceDb{0.0};   // against the mix: -6 is a stem 6 dB under the whole
    double activeShare{0.0}; // of the song
};

struct Reading
{
    std::string name;   // "Chanson.wav", for the eye
    std::string digest; // of the file: the reference is named by it, never by a path
    double seconds{0.0};

    std::optional<double> bpm;
    double bpmConfidence{0.0}; // 0..1
    std::optional<generation::Key> key;
    double keyConfidence{0.0}; // 0..1: the margin between the best key and the next
    // The two keys that fit best, when any fits at all: what the panel offers
    // to choose from when the key is not trusted ("la mineur ou do majeur ?").
    std::vector<generation::Key> keyCandidates;

    std::vector<Section> sections;
    std::map<std::string, StemReading> stems;

    // The mix, as the S20 measure reads a master.
    std::array<double, mix::bandCount> tilt{}; // each octave minus their mean, dB
    double crestDb{0.0};
    double sideShare{0.0};
    double loudnessLufs{mix::silenceDb};

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Reading> fromValue(const Value& value);

    friend bool operator==(const Reading& lhs, const Reading& rhs) = default;
};

// The thresholds under which a value is left out.
inline constexpr double tempoTrusted = 0.35; // normalised autocorrelation of the onsets at the beat
inline constexpr double keyFitFloor = 0.5;   // correlation of the best key: under it, no harmony is read
inline constexpr double keyTrusted = 0.10;   // margin of the best key over the next

// Reads a reference from its stems. All four the same length, at `rate`.
[[nodiscard]] Reading
read(const std::map<std::string, Stereo>& stems, double rate, std::string name, std::string digest);

// The parts, exposed for their tests.
struct TempoEstimate
{
    double bpm{0.0};
    double confidence{0.0};
};
[[nodiscard]] TempoEstimate estimateTempo(const std::vector<float>& mono, double rate);

struct KeyEstimate
{
    generation::Key key;
    generation::Key second;
    double fit{0.0};        // correlation of the best key
    double confidence{0.0}; // its margin over the second, 0 when it fits under keyFitFloor
};
[[nodiscard]] KeyEstimate estimateKey(const std::vector<float>& mono, double rate);

} // namespace daw::domain::direction
