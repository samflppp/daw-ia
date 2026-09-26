#include "daw/domain/generation/Harmony.h"

#include <cmath>

namespace daw::domain::generation
{
namespace
{

constexpr int semitones = 12;

[[nodiscard]] int pitchClass(int pitch) noexcept
{
    return ((pitch % semitones) + semitones) % semitones;
}

// Krumhansl and Kessler, 1982: how well each pitch class fits a key, heard by
// listeners. The standard profiles, rotated to the tonic being tried.
constexpr std::array<double, 12> majorProfile{
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> minorProfile{
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

[[nodiscard]] double
correlation(const std::array<double, 12>& heard, const std::array<double, 12>& profile, int tonic)
{
    double meanHeard = 0.0;
    double meanProfile = 0.0;
    for (int i = 0; i < semitones; ++i)
    {
        meanHeard += heard[static_cast<std::size_t>(i)];
        meanProfile += profile[static_cast<std::size_t>(i)];
    }
    meanHeard /= semitones;
    meanProfile /= semitones;

    double covariance = 0.0;
    double varianceHeard = 0.0;
    double varianceProfile = 0.0;
    for (int i = 0; i < semitones; ++i)
    {
        const auto h = heard[static_cast<std::size_t>(pitchClass(i + tonic))] - meanHeard;
        const auto p = profile[static_cast<std::size_t>(i)] - meanProfile;
        covariance += h * p;
        varianceHeard += h * h;
        varianceProfile += p * p;
    }

    if (varianceHeard <= 0.0 || varianceProfile <= 0.0)
        return 0.0;
    return covariance / std::sqrt(varianceHeard * varianceProfile);
}

// A major key and its relative minor share every note, and the profiles often
// split them by a hair: Am F C G reads as C major. The music this DAW is for
// is mostly minor, so a near tie goes to minor. It changes which degree the
// style model calls the tonic, never which pitches are legal.
constexpr double minorBias = 0.05;

} // namespace

int stepsOf(Resolution resolution) noexcept
{
    switch (resolution)
    {
    case Resolution::quarter:
        return 4;
    case Resolution::eighth:
        return 2;
    case Resolution::sixteenth:
        return 1;
    }
    return 1;
}

const std::array<int, 7>& scaleOf(Mode mode) noexcept
{
    static constexpr std::array<int, 7> major{0, 2, 4, 5, 7, 9, 11};
    static constexpr std::array<int, 7> minor{0, 2, 3, 5, 7, 8, 10};
    return mode == Mode::major ? major : minor;
}

std::optional<int> diatonicIndex(int pitch, Key key) noexcept
{
    const auto fromTonic = pitch - key.tonic;
    const auto octave = fromTonic >= 0 ? fromTonic / semitones : -((-fromTonic + semitones - 1) / semitones);
    const auto within = fromTonic - octave * semitones;

    const auto& scale = scaleOf(key.mode);
    for (int degree = 0; degree < 7; ++degree)
    {
        if (scale[static_cast<std::size_t>(degree)] == within)
            return octave * 7 + degree;
    }
    return std::nullopt;
}

int pitchOfIndex(int index, Key key) noexcept
{
    const auto octave = index >= 0 ? index / 7 : -((-index + 6) / 7);
    const auto degree = index - octave * 7;
    return key.tonic + octave * semitones + scaleOf(key.mode)[static_cast<std::size_t>(degree)];
}

bool inScale(int pitch, Key key) noexcept
{
    return diatonicIndex(pitch, key).has_value();
}

std::vector<int> legalPitches(Key key, int low, int high)
{
    std::vector<int> out;
    for (int pitch = low; pitch <= high; ++pitch)
    {
        if (pitch >= 0 && pitch <= 127 && inScale(pitch, key))
            out.push_back(pitch);
    }
    return out;
}

std::pair<int, int> registerRange(Role role, Register reg) noexcept
{
    // Written for a trap and drill palette: an 808 lives under C2, a lead
    // around C5, a pad in between. A register is a window of about an octave
    // and a half, wide enough for a phrase, narrow enough to mean something.
    switch (role)
    {
    case Role::bass:
        switch (reg)
        {
        case Register::low:
            return {28, 45}; // E1 .. A2
        case Register::mid:
            return {36, 55};
        case Register::high:
            return {43, 62};
        }
        break;
    case Role::chords:
        switch (reg)
        {
        case Register::low:
            return {45, 67};
        case Register::mid:
            return {53, 76};
        case Register::high:
            return {60, 84};
        }
        break;
    case Role::melody:
    case Role::rhythm:
        switch (reg)
        {
        case Register::low:
            return {48, 67};
        case Register::mid:
            return {60, 79};
        case Register::high:
            return {69, 91};
        }
        break;
    }
    return {60, 79};
}

std::optional<Key> detectKey(const std::vector<WeightedPitch>& pitches)
{
    std::array<double, 12> heard{};
    double total = 0.0;
    for (const auto& note : pitches)
    {
        heard[static_cast<std::size_t>(pitchClass(note.pitch))] += note.weight;
        total += note.weight;
    }
    if (total <= 0.0)
        return std::nullopt;

    std::optional<Key> best;
    double bestScore = -2.0;
    for (int tonic = 0; tonic < semitones; ++tonic)
    {
        for (const auto mode : {Mode::minor, Mode::major})
        {
            auto score = correlation(heard, mode == Mode::major ? majorProfile : minorProfile, tonic);
            if (mode == Mode::minor)
                score += minorBias;
            if (score > bestScore + 1e-9)
            {
                bestScore = score;
                best = Key{tonic, mode};
            }
        }
    }
    return best;
}

std::array<int, 3> chordDegrees(Chord chord) noexcept
{
    return {chord.root % 7, (chord.root + 2) % 7, (chord.root + 4) % 7};
}

bool isDiminished(Chord chord, Key key) noexcept
{
    return (key.mode == Mode::major && chord.root % 7 == 6) ||
           (key.mode == Mode::minor && chord.root % 7 == 1);
}

bool isChordTone(int pitch, Chord chord, Key key) noexcept
{
    const auto index = diatonicIndex(pitch, key);
    if (!index.has_value())
        return false;

    const auto degree = ((*index % 7) + 7) % 7;
    for (const auto tone : chordDegrees(chord))
    {
        if (tone == degree)
            return true;
    }
    return false;
}

std::optional<Chord> detectChord(const std::vector<WeightedPitch>& pitches, Key key)
{
    std::array<double, 7> byDegree{};
    double total = 0.0;
    for (const auto& note : pitches)
    {
        const auto index = diatonicIndex(note.pitch, key);
        if (!index.has_value())
            continue;
        byDegree[static_cast<std::size_t>(((*index % 7) + 7) % 7)] += note.weight;
        total += note.weight;
    }
    if (total <= 0.0)
        return std::nullopt;

    // The root counts a little more than the third and the fifth: A C E is A
    // minor and not C major with an A in it.
    std::optional<Chord> best;
    double bestScore = 0.0;
    for (int root = 0; root < 7; ++root)
    {
        const Chord chord{root};
        if (isDiminished(chord, key))
            continue;

        const auto degrees = chordDegrees(chord);
        const auto score = byDegree[static_cast<std::size_t>(degrees[0])] * 1.2 +
                           byDegree[static_cast<std::size_t>(degrees[1])] +
                           byDegree[static_cast<std::size_t>(degrees[2])];
        if (score > bestScore + 1e-9)
        {
            bestScore = score;
            best = chord;
        }
    }
    return best;
}

} // namespace daw::domain::generation
