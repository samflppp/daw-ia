#pragma once

#include <cstddef>
#include <vector>

namespace daw::domain::flux
{

// What a node of the audio flux shows (S24), computed from the samples a tap
// holds: the shape of the sound, its level, and — for the before and the
// after of an effect — its spectrum. Pure arithmetic on numbers handed in;
// the screen only draws what comes out.

// The lowest and highest sample of one column of a drawn waveform.
struct Span
{
    float low{0.0f};
    float high{0.0f};
};

// `count` samples drawn in `columns` columns: each column the extremes of
// the samples that fall in it. A column no sample falls in (more columns
// than samples) repeats the sample under it. No samples, no columns.
[[nodiscard]] std::vector<Span> envelopeOf(const float* samples, std::size_t count, std::size_t columns);

// The peak of the samples in dBFS, `floorDb` for silence.
[[nodiscard]] double peakDbOf(const float* samples, std::size_t count, double floorDb = -100.0);

// The spectrum of the last 4096 samples (fewer read as zeros before them),
// under a Hann window, in `bands` bands spaced evenly in log frequency from
// `lowHz` to `highHz`: each band the level of its strongest bin, in dB
// relative to a full-scale sine (a sine at 0 dBFS reads 0 dB in its band),
// never under `floorDb`.
inline constexpr std::size_t spectrumSize = 4096;
[[nodiscard]] std::vector<double> spectrumOf(const float* samples,
                                             std::size_t count,
                                             double sampleRate,
                                             std::size_t bands,
                                             double lowHz = 30.0,
                                             double highHz = 16000.0,
                                             double floorDb = -100.0);

// The frequency at the middle of band `band` of `bands`, on the same scale.
[[nodiscard]] double
bandHz(std::size_t band, std::size_t bands, double lowHz = 30.0, double highHz = 16000.0);

} // namespace daw::domain::flux
