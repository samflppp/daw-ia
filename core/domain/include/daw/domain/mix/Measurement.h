#pragma once

#include "daw/domain/Value.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace daw::domain::mix
{

// What the mix hears (S20). Measuring is not deciding: this file only turns
// samples into numbers, locally, deterministically, the same way every time.
// It guesses nothing — a role, a taste, a target are decided elsewhere, from
// these numbers.
//
// The engine renders each track (after its inserts, before its fader) and the
// master, and hands the samples here as they come out of the render. Nothing
// of the audio is kept: an analyser holds per-hop numbers, 100 ms apart.
//
// Pure C++, like the generator: the loudness is checked against the signals of
// EBU Tech 3341 in the CI, without an engine.

// Ten octave bands, ISO centres 31.5 Hz to 16 kHz, edges at centre/√2 and
// centre·√2. An octave is the resolution a mixing engineer talks in ("le
// bas-médium, vers 250"), and ten numbers per track keep the decision small.
inline constexpr std::size_t bandCount = 10;
inline constexpr std::array<double, bandCount> bandCentres{
    31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
[[nodiscard]] double bandLow(std::size_t band) noexcept;
[[nodiscard]] double bandHigh(std::size_t band) noexcept;

// The floor of every level here: what silence reads as.
inline constexpr double silenceDb = -120.0;

// The hop of every running figure: 100 ms, the hop BS.1770 gates with.
inline constexpr double hopSeconds = 0.1;

// What one stream measured, over the whole range rendered.
struct StreamMeasure
{
    double seconds{0.0};

    // ITU-R BS.1770-4: K-weighted, both channels summed, gated at -70 LUFS
    // absolute and -10 LU relative over 400 ms blocks overlapping by 75 %.
    double integratedLufs{silenceDb};
    double shortTermMaxLufs{silenceDb}; // 3 s windows
    double momentaryMaxLufs{silenceDb}; // 400 ms windows

    // 4× oversampled, BS.1770-4 annex 2; and the plain sample peak.
    double truePeakDb{silenceDb};
    double samplePeakDb{silenceDb};

    // The loudest 10 ms against the RMS of the blocks that play, in dB. Not
    // the sample peak against the RMS: a compressor without look-ahead lets
    // the first cycle of a hit through, and a peak-based figure would not
    // say what it did to the hit.
    double crestDb{0.0};

    // Mean level of each octave band over the hops where the stream plays,
    // as a mean square in dBFS: a sine of amplitude 1 in a band reads -3 dB.
    std::array<double, bandCount> bandsDb{};

    // L/R correlation, 1 mono to -1 opposite; and the share of the energy in
    // the side signal, 0 for mono, 0.5 for two unrelated channels.
    double correlation{1.0};
    double sideShare{0.0};

    // The part of the range where the stream plays: 400 ms blocks louder than
    // -60 LUFS and than 20 LU under its integrated loudness.
    double activeShare{0.0};

    // Per hop: the band levels (dBFS) and whether the stream plays there.
    // What masking reads; never sent to a model.
    std::vector<std::array<float, bandCount>> hopBands;
    std::vector<bool> hopActive;

    [[nodiscard]] Value toValue() const; // the summary, rounded to 0.1, without the hops
};

// The analyser of one stream. Feed it blocks as the render produces them, in
// order, then ask for the measure. One per track, one for the master.
class StreamAnalyser
{
public:
    explicit StreamAnalyser(double sampleRate);
    ~StreamAnalyser();
    StreamAnalyser(StreamAnalyser&&) noexcept;
    StreamAnalyser& operator=(StreamAnalyser&&) noexcept;
    StreamAnalyser(const StreamAnalyser&) = delete;
    StreamAnalyser& operator=(const StreamAnalyser&) = delete;

    // `right` may be the same pointer as `left` for a mono stream.
    void process(const float* left, const float* right, std::size_t count);

    [[nodiscard]] StreamMeasure finish() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Two streams that play the same band at the same level at the same time: the
// ear cannot tell them apart there. A kick and a bass in the 63 Hz octave, a
// voice and the chords around 2 kHz.
struct Overlap
{
    std::size_t first{0};  // index of a stream
    std::size_t second{0}; // index of another, greater
    std::size_t band{0};
    double share{0.0};   // of the hops where both play, the part where they mask each other
    double levelDb{0.0}; // mean level of the louder one there

    struct Span
    {
        double fromSeconds{0.0};
        double toSeconds{0.0};
    };
    std::vector<Span> spans; // the longest stretches, at most three, at least 0.5 s each
};

// Every overlap worth naming, worst first: at least 10 % of the shared time.
// Two streams mask each other in a band at a hop when both play, the band is
// within 20 dB of each one's loudest band at that hop, and their two levels
// are within 6 dB of each other.
[[nodiscard]] std::vector<Overlap> overlaps(const std::vector<StreamMeasure>& streams);

[[nodiscard]] std::string bandName(std::size_t band); // "63 Hz", "1 kHz"

} // namespace daw::domain::mix
