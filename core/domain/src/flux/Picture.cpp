#include "daw/domain/flux/Picture.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

#include "../dsp/Fft.h"

namespace daw::domain::flux
{
namespace
{

constexpr std::size_t spectrumOrder = 12; // 4096

const dsp::Fft& transform()
{
    static const dsp::Fft fft{spectrumOrder};
    return fft;
}

// The Hann window, and what it takes from a sine's amplitude (its mean, 0.5):
// divided back so that a full-scale sine reads 0 dB.
const std::vector<double>& window()
{
    static const std::vector<double> values = []
    {
        std::vector<double> hann(spectrumSize);
        for (std::size_t index = 0; index < spectrumSize; ++index)
            hann[index] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) /
                                               static_cast<double>(spectrumSize));
        return hann;
    }();
    return values;
}

double toDb(double gain, double floorDb)
{
    return gain > 0.0 ? std::max(floorDb, 20.0 * std::log10(gain)) : floorDb;
}

double edgeHz(std::size_t edge, std::size_t bands, double lowHz, double highHz)
{
    return lowHz * std::pow(highHz / lowHz, static_cast<double>(edge) / static_cast<double>(bands));
}

} // namespace

std::vector<Span> envelopeOf(const float* samples, std::size_t count, std::size_t columns)
{
    std::vector<Span> spans;
    if (samples == nullptr || count == 0 || columns == 0)
        return spans;

    spans.resize(columns);
    for (std::size_t column = 0; column < columns; ++column)
    {
        auto first = column * count / columns;
        auto last = (column + 1) * count / columns;
        if (last <= first)
            last = std::min(count, first + 1);
        first = std::min(first, count - 1);

        auto low = samples[first];
        auto high = samples[first];
        for (auto index = first + 1; index < last; ++index)
        {
            low = std::min(low, samples[index]);
            high = std::max(high, samples[index]);
        }
        spans[column] = Span{low, high};
    }
    return spans;
}

double peakDbOf(const float* samples, std::size_t count, double floorDb)
{
    float peak = 0.0f;
    for (std::size_t index = 0; samples != nullptr && index < count; ++index)
        peak = std::max(peak, std::abs(samples[index]));
    return toDb(static_cast<double>(peak), floorDb);
}

std::vector<double> spectrumOf(const float* samples,
                               std::size_t count,
                               double sampleRate,
                               std::size_t bands,
                               double lowHz,
                               double highHz,
                               double floorDb)
{
    std::vector<double> levels(bands, floorDb);
    if (samples == nullptr || bands == 0 || sampleRate <= 0.0 || lowHz <= 0.0 || highHz <= lowHz)
        return levels;

    // The last spectrumSize samples, right-aligned: what is missing before
    // them reads as silence.
    std::vector<std::complex<double>> data(spectrumSize);
    const auto used = std::min(count, spectrumSize);
    const auto offset = spectrumSize - used;
    const auto& hann = window();
    for (std::size_t index = 0; index < used; ++index)
        data[offset + index] = {static_cast<double>(samples[count - used + index]) * hann[offset + index],
                                0.0};
    transform()(data);

    // A sine of amplitude A peaks at A × N/2 × 0.5 (the window's mean).
    const auto scale = 4.0 / static_cast<double>(spectrumSize);
    const auto binHz = sampleRate / static_cast<double>(spectrumSize);
    for (std::size_t band = 0; band < bands; ++band)
    {
        const auto from = edgeHz(band, bands, lowHz, highHz);
        const auto to = edgeHz(band + 1, bands, lowHz, highHz);
        // At least the bin nearest the band's middle: a narrow low band
        // between two bins still reads something.
        auto first = static_cast<std::size_t>(std::ceil(from / binHz));
        auto last = static_cast<std::size_t>(std::floor(to / binHz));
        if (last < first)
            first = last = static_cast<std::size_t>(std::llround(std::sqrt(from * to) / binHz));
        last = std::min(last, spectrumSize / 2);

        double strongest = 0.0;
        for (auto bin = first; bin <= last; ++bin)
            strongest = std::max(strongest, std::abs(data[bin]) * scale);
        levels[band] = toDb(strongest, floorDb);
    }
    return levels;
}

double bandHz(std::size_t band, std::size_t bands, double lowHz, double highHz)
{
    if (bands == 0)
        return lowHz;
    return std::sqrt(edgeHz(band, bands, lowHz, highHz) * edgeHz(band + 1, bands, lowHz, highHz));
}

} // namespace daw::domain::flux
