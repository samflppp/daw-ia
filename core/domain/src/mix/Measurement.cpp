#include "daw/domain/mix/Measurement.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

namespace daw::domain::mix
{
namespace
{

constexpr std::size_t fftOrder = 12;
constexpr std::size_t fftSize = std::size_t{1} << fftOrder;

double toDb(double meanSquare)
{
    return meanSquare > 1e-12 ? 10.0 * std::log10(meanSquare) : silenceDb;
}

double lufsOf(double weightedMeanSquare)
{
    return weightedMeanSquare > 1e-12 ? -0.691 + 10.0 * std::log10(weightedMeanSquare) : silenceDb;
}

double rounded(double value)
{
    return std::round(value * 10.0) / 10.0;
}

// A direct form I biquad, normalised (a0 = 1).
struct Biquad
{
    double b0{1.0}, b1{0.0}, b2{0.0}, a1{0.0}, a2{0.0};
    double x1{0.0}, x2{0.0}, y1{0.0}, y2{0.0};

    double operator()(double x)
    {
        const auto y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// The K-weighting of BS.1770-4 at any sample rate: its high shelf and its
// high-pass, from the analogue prototypes the 48 kHz coefficients of the
// standard were drawn from (the way libebur128 computes them).
std::pair<Biquad, Biquad> kWeighting(double sampleRate)
{
    Biquad shelf;
    {
        constexpr double f0 = 1681.974450955533;
        constexpr double gain = 3.999843853973347;
        constexpr double q = 0.7071752369554196;
        const auto k = std::tan(std::numbers::pi * f0 / sampleRate);
        const auto vh = std::pow(10.0, gain / 20.0);
        const auto vb = std::pow(vh, 0.4996667741545416);
        const auto a0 = 1.0 + k / q + k * k;
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }
    Biquad highPass;
    {
        constexpr double f0 = 38.13547087602444;
        constexpr double q = 0.5003270373238773;
        const auto k = std::tan(std::numbers::pi * f0 / sampleRate);
        const auto a0 = 1.0 + k / q + k * k;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (k * k - 1.0) / a0;
        highPass.a2 = (1.0 - k / q + k * k) / a0;
    }
    return {shelf, highPass};
}

// 4× oversampling for the true peak: a 48-tap windowed sinc, twelve taps per
// phase, each phase normalised to unity at DC.
struct TruePeakFilter
{
    static constexpr std::size_t taps = 12;
    std::array<std::array<double, taps>, 4> phases{};

    TruePeakFilter()
    {
        constexpr double length = 4.0 * taps;
        for (std::size_t phase = 0; phase < 4; ++phase)
        {
            double sum = 0.0;
            for (std::size_t tap = 0; tap < taps; ++tap)
            {
                const auto n = static_cast<double>(tap * 4 + phase);
                const auto centred = (n - (length - 1.0) / 2.0) / 4.0;
                const auto sinc = std::abs(centred) < 1e-12
                                      ? 1.0
                                      : std::sin(std::numbers::pi * centred) / (std::numbers::pi * centred);
                const auto window = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * n / (length - 1.0)) +
                                    0.08 * std::cos(4.0 * std::numbers::pi * n / (length - 1.0));
                phases[phase][tap] = sinc * window;
                sum += phases[phase][tap];
            }
            for (auto& coefficient : phases[phase])
                coefficient /= sum;
        }
    }
};

const TruePeakFilter& truePeakFilter()
{
    static const TruePeakFilter filter;
    return filter;
}

// An iterative radix-2 FFT of fftSize complex points.
struct Fft
{
    std::vector<std::complex<double>> twiddles;
    std::vector<std::size_t> reversed;

    Fft()
        : twiddles(fftSize / 2)
        , reversed(fftSize)
    {
        for (std::size_t index = 0; index < fftSize / 2; ++index)
            twiddles[index] = std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(index) / fftSize);
        for (std::size_t index = 0; index < fftSize; ++index)
        {
            std::size_t value = 0;
            for (std::size_t bit = 0; bit < fftOrder; ++bit)
                value |= ((index >> bit) & 1U) << (fftOrder - 1 - bit);
            reversed[index] = value;
        }
    }

    void operator()(std::vector<std::complex<double>>& data) const
    {
        for (std::size_t index = 0; index < fftSize; ++index)
        {
            if (index < reversed[index])
                std::swap(data[index], data[reversed[index]]);
        }
        for (std::size_t size = 2; size <= fftSize; size *= 2)
        {
            const auto half = size / 2;
            const auto stride = fftSize / size;
            for (std::size_t start = 0; start < fftSize; start += size)
            {
                for (std::size_t offset = 0; offset < half; ++offset)
                {
                    const auto product = data[start + offset + half] * twiddles[offset * stride];
                    data[start + offset + half] = data[start + offset] - product;
                    data[start + offset] += product;
                }
            }
        }
    }
};

const Fft& fft()
{
    static const Fft transform;
    return transform;
}

struct Hann
{
    std::vector<double> values;
    double sumOfSquares{0.0};

    Hann()
        : values(fftSize)
    {
        for (std::size_t index = 0; index < fftSize; ++index)
        {
            values[index] =
                0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) / fftSize);
            sumOfSquares += values[index] * values[index];
        }
    }
};

const Hann& hann()
{
    static const Hann window;
    return window;
}

} // namespace

double bandLow(std::size_t band) noexcept
{
    return bandCentres[band] / std::numbers::sqrt2;
}

double bandHigh(std::size_t band) noexcept
{
    return bandCentres[band] * std::numbers::sqrt2;
}

std::string bandName(std::size_t band)
{
    const auto centre = bandCentres[band];
    if (centre >= 1000.0)
        return std::to_string(static_cast<int>(centre / 1000.0)) + " kHz";
    if (centre == 31.5)
        return "31,5 Hz";
    return std::to_string(static_cast<int>(centre)) + " Hz";
}

Value StreamMeasure::toValue() const
{
    Value::Array bands;
    for (const auto level : bandsDb)
        bands.push_back(Value{rounded(level)});
    return Value::object({{"lufs", Value{rounded(integratedLufs)}},
                          {"lufsShortMax", Value{rounded(shortTermMaxLufs)}},
                          {"truePeak", Value{rounded(truePeakDb)}},
                          {"crest", Value{rounded(crestDb)}},
                          {"bands", Value::array(std::move(bands))},
                          {"correlation", Value{std::round(correlation * 100.0) / 100.0}},
                          {"side", Value{std::round(sideShare * 100.0) / 100.0}},
                          {"active", Value{std::round(activeShare * 100.0) / 100.0}}});
}

// --- the analyser ----------------------------------------------------------------

struct StreamAnalyser::State
{
    double sampleRate;
    std::size_t hopSamples;
    std::size_t crestSamples;

    std::array<Biquad, 2> shelf;
    std::array<Biquad, 2> highPass;

    // The hop being filled.
    double hopWeighted{0.0}; // K-weighted, both channels summed
    double hopRaw{0.0};      // (L² + R²) / 2
    std::size_t inHop{0};

    // The 10 ms being filled, and the loudest one so far.
    double crestRaw{0.0};
    std::size_t inCrest{0};
    double loudestCrest{0.0};

    double sumLL{0.0}, sumRR{0.0}, sumLR{0.0};
    double peak{0.0};
    double truePeak{0.0};
    std::size_t samples{0};

    // The last fftSize samples of each channel, and the last twelve for the
    // true peak.
    std::vector<double> ringLeft, ringRight;
    std::size_t ringAt{0};
    std::array<std::array<double, TruePeakFilter::taps>, 2> recent{};
    std::size_t recentAt{0};

    std::vector<double> hopWeightedValues;
    std::vector<double> hopRawValues;
    std::vector<std::array<float, bandCount>> hopBands;
    std::vector<std::array<double, bandCount>> hopBandPower;

    std::vector<std::complex<double>> spectrum;
    std::vector<std::array<std::size_t, 2>> bandBins;

    explicit State(double rate)
        : sampleRate{rate}
        , hopSamples{static_cast<std::size_t>(std::lround(rate * hopSeconds))}
        , crestSamples{static_cast<std::size_t>(std::lround(rate * 0.010))}
        , ringLeft(fftSize, 0.0)
        , ringRight(fftSize, 0.0)
        , spectrum(fftSize)
    {
        const auto [s, h] = kWeighting(rate);
        shelf = {s, s};
        highPass = {h, h};
        for (std::size_t band = 0; band < bandCount; ++band)
        {
            const auto from = static_cast<std::size_t>(std::ceil(bandLow(band) * fftSize / rate));
            const auto to = static_cast<std::size_t>(std::ceil(bandHigh(band) * fftSize / rate));
            bandBins.push_back({std::max<std::size_t>(from, 1), std::min(to, fftSize / 2)});
        }
    }

    void truePeakAt(std::size_t channel)
    {
        // The interpolated values between the samples around the centre of
        // the twelve last. Computed only near the loudest so far: away from
        // it, an inter-sample peak cannot catch up the 6 dB of margin.
        const auto& history = recent[channel];
        const auto centre = history[(recentAt + TruePeakFilter::taps - 6) % TruePeakFilter::taps];
        const auto next = history[(recentAt + TruePeakFilter::taps - 5) % TruePeakFilter::taps];
        if (std::max(std::abs(centre), std::abs(next)) < 0.5 * truePeak)
            return;
        for (const auto& phase : truePeakFilter().phases)
        {
            double value = 0.0;
            for (std::size_t tap = 0; tap < TruePeakFilter::taps; ++tap)
                value +=
                    phase[tap] * history[(recentAt + TruePeakFilter::taps - 1 - tap) % TruePeakFilter::taps];
            truePeak = std::max(truePeak, std::abs(value));
        }
    }

    void closeHop()
    {
        hopWeightedValues.push_back(hopWeighted / static_cast<double>(hopSamples));
        hopRawValues.push_back(hopRaw / static_cast<double>(hopSamples));
        hopWeighted = 0.0;
        hopRaw = 0.0;
        inHop = 0;

        // Both channels in one transform: left in the real part, right in the
        // imaginary part, separated by symmetry.
        const auto& window = hann();
        for (std::size_t index = 0; index < fftSize; ++index)
        {
            const auto at = (ringAt + index) % fftSize;
            spectrum[index] = {ringLeft[at] * window.values[index], ringRight[at] * window.values[index]};
        }
        fft()(spectrum);
        const auto scale = 2.0 / (static_cast<double>(fftSize) * window.sumOfSquares);
        std::array<double, bandCount> power{};
        std::array<float, bandCount> levels{};
        for (std::size_t band = 0; band < bandCount; ++band)
        {
            double sum = 0.0;
            for (auto bin = bandBins[band][0]; bin < bandBins[band][1]; ++bin)
            {
                const auto z = spectrum[bin];
                const auto mirror = std::conj(spectrum[fftSize - bin]);
                const auto left = (z + mirror) * 0.5;
                const auto right = (z - mirror) * std::complex<double>{0.0, -0.5};
                sum += 0.5 * (std::norm(left) + std::norm(right));
            }
            power[band] = sum * scale;
            levels[band] = static_cast<float>(toDb(power[band]));
        }
        hopBandPower.push_back(power);
        hopBands.push_back(levels);
    }
};

StreamAnalyser::StreamAnalyser(double sampleRate)
    : state_{std::make_unique<State>(sampleRate)}
{
}

StreamAnalyser::~StreamAnalyser() = default;
StreamAnalyser::StreamAnalyser(StreamAnalyser&&) noexcept = default;
StreamAnalyser& StreamAnalyser::operator=(StreamAnalyser&&) noexcept = default;

void StreamAnalyser::process(const float* left, const float* right, std::size_t count)
{
    auto& s = *state_;
    for (std::size_t index = 0; index < count; ++index)
    {
        const double l = left[index];
        const double r = right[index];

        const auto kl = s.highPass[0](s.shelf[0](l));
        const auto kr = s.highPass[1](s.shelf[1](r));
        s.hopWeighted += kl * kl + kr * kr;
        const auto raw = 0.5 * (l * l + r * r);
        s.hopRaw += raw;
        s.crestRaw += raw;
        s.sumLL += l * l;
        s.sumRR += r * r;
        s.sumLR += l * r;
        s.peak = std::max({s.peak, std::abs(l), std::abs(r)});

        s.recent[0][s.recentAt] = l;
        s.recent[1][s.recentAt] = r;
        s.recentAt = (s.recentAt + 1) % TruePeakFilter::taps;
        s.truePeak = std::max(s.truePeak, std::max(std::abs(l), std::abs(r)));
        s.truePeakAt(0);
        s.truePeakAt(1);

        s.ringLeft[s.ringAt] = l;
        s.ringRight[s.ringAt] = r;
        s.ringAt = (s.ringAt + 1) % fftSize;

        ++s.samples;
        if (++s.inCrest == s.crestSamples)
        {
            s.loudestCrest = std::max(s.loudestCrest, s.crestRaw / static_cast<double>(s.crestSamples));
            s.crestRaw = 0.0;
            s.inCrest = 0;
        }
        if (++s.inHop == s.hopSamples)
            s.closeHop();
    }
}

StreamMeasure StreamAnalyser::finish() const
{
    const auto& s = *state_;
    StreamMeasure out;
    out.seconds = static_cast<double>(s.samples) / s.sampleRate;
    out.samplePeakDb = s.peak > 0.0 ? 20.0 * std::log10(s.peak) : silenceDb;
    out.truePeakDb = s.truePeak > 0.0 ? 20.0 * std::log10(s.truePeak) : silenceDb;

    const auto hops = s.hopWeightedValues.size();
    const auto window = [&s](std::size_t from, std::size_t length)
    {
        double sum = 0.0;
        for (std::size_t hop = from; hop < from + length; ++hop)
            sum += s.hopWeightedValues[hop];
        return sum / static_cast<double>(length);
    };

    // 400 ms blocks every 100 ms, gated.
    std::vector<double> blocks;
    for (std::size_t hop = 0; hop + 4 <= hops; ++hop)
        blocks.push_back(window(hop, 4));
    for (const auto block : blocks)
        out.momentaryMaxLufs = std::max(out.momentaryMaxLufs, lufsOf(block));
    for (std::size_t hop = 0; hop + 30 <= hops; ++hop)
        out.shortTermMaxLufs = std::max(out.shortTermMaxLufs, lufsOf(window(hop, 30)));

    const auto gatedMean = [&blocks](double threshold)
    {
        double sum = 0.0;
        std::size_t kept = 0;
        for (const auto block : blocks)
        {
            if (lufsOf(block) > threshold)
            {
                sum += block;
                ++kept;
            }
        }
        return kept > 0 ? sum / static_cast<double>(kept) : 0.0;
    };
    const auto absolute = gatedMean(-70.0);
    if (absolute > 0.0)
        out.integratedLufs = lufsOf(gatedMean(lufsOf(absolute) - 10.0));

    // What plays: blocks over -60 LUFS and within 20 LU of the whole.
    const auto floor = std::max(-60.0, out.integratedLufs - 20.0);
    out.hopActive.assign(hops, false);
    std::size_t activeBlocks = 0;
    for (std::size_t block = 0; block < blocks.size(); ++block)
    {
        if (lufsOf(blocks[block]) >= floor)
        {
            ++activeBlocks;
            for (std::size_t hop = block; hop < block + 4; ++hop)
                out.hopActive[hop] = true;
        }
    }
    out.activeShare =
        blocks.empty() ? 0.0 : static_cast<double>(activeBlocks) / static_cast<double>(blocks.size());

    double activeRaw = 0.0;
    std::size_t activeHops = 0;
    std::array<double, bandCount> bandSum{};
    for (std::size_t hop = 0; hop < hops; ++hop)
    {
        if (!out.hopActive[hop])
            continue;
        activeRaw += s.hopRawValues[hop];
        ++activeHops;
        for (std::size_t band = 0; band < bandCount; ++band)
            bandSum[band] += s.hopBandPower[hop][band];
    }
    for (std::size_t band = 0; band < bandCount; ++band)
        out.bandsDb[band] =
            activeHops > 0 ? toDb(bandSum[band] / static_cast<double>(activeHops)) : silenceDb;
    if (activeHops > 0 && activeRaw > 0.0 && s.loudestCrest > 0.0)
        out.crestDb = 10.0 * std::log10(s.loudestCrest / (activeRaw / static_cast<double>(activeHops)));

    const auto energy = s.sumLL + s.sumRR;
    if (s.sumLL > 0.0 && s.sumRR > 0.0)
        out.correlation = s.sumLR / std::sqrt(s.sumLL * s.sumRR);
    if (energy > 0.0)
        out.sideShare = std::max(0.0, (energy - 2.0 * s.sumLR) / (2.0 * energy));

    out.hopBands = s.hopBands;
    return out;
}

// --- masking ----------------------------------------------------------------

std::vector<Overlap> overlaps(const std::vector<StreamMeasure>& streams)
{
    constexpr double significantDb = 20.0;
    constexpr double closeDb = 6.0;
    constexpr double floorDb = -80.0;
    constexpr std::size_t minimumSpan = 5; // hops: 0.5 s
    constexpr std::size_t allowedGap = 2;

    std::vector<Overlap> found;
    for (std::size_t first = 0; first < streams.size(); ++first)
    {
        for (std::size_t second = first + 1; second < streams.size(); ++second)
        {
            const auto& a = streams[first];
            const auto& b = streams[second];
            const auto hops =
                std::min({a.hopBands.size(), b.hopBands.size(), a.hopActive.size(), b.hopActive.size()});

            std::size_t both = 0;
            std::array<std::vector<bool>, bandCount> masked;
            for (auto& band : masked)
                band.assign(hops, false);
            for (std::size_t hop = 0; hop < hops; ++hop)
            {
                if (!a.hopActive[hop] || !b.hopActive[hop])
                    continue;
                ++both;
                const auto loudestA = *std::max_element(a.hopBands[hop].begin(), a.hopBands[hop].end());
                const auto loudestB = *std::max_element(b.hopBands[hop].begin(), b.hopBands[hop].end());
                for (std::size_t band = 0; band < bandCount; ++band)
                {
                    const double levelA = a.hopBands[hop][band];
                    const double levelB = b.hopBands[hop][band];
                    masked[band][hop] =
                        levelA > floorDb && levelB > floorDb && levelA >= loudestA - significantDb &&
                        levelB >= loudestB - significantDb && std::abs(levelA - levelB) <= closeDb;
                }
            }
            if (both == 0)
                continue;

            for (std::size_t band = 0; band < bandCount; ++band)
            {
                std::size_t count = 0;
                double level = 0.0;
                for (std::size_t hop = 0; hop < hops; ++hop)
                {
                    if (!masked[band][hop])
                        continue;
                    ++count;
                    level += std::max(a.hopBands[hop][band], b.hopBands[hop][band]);
                }
                const auto share = static_cast<double>(count) / static_cast<double>(both);
                if (share < 0.1)
                    continue;

                Overlap overlap;
                overlap.first = first;
                overlap.second = second;
                overlap.band = band;
                overlap.share = share;
                overlap.levelDb = level / static_cast<double>(count);

                // Stretches, gaps of two hops forgiven.
                std::vector<std::pair<std::size_t, std::size_t>> stretches;
                std::size_t hop = 0;
                while (hop < hops)
                {
                    if (!masked[band][hop])
                    {
                        ++hop;
                        continue;
                    }
                    auto end = hop;
                    auto last = hop;
                    while (end < hops && end <= last + allowedGap + 1)
                    {
                        if (masked[band][end])
                            last = end;
                        ++end;
                    }
                    stretches.emplace_back(hop, last + 1);
                    hop = last + 1;
                }
                std::stable_sort(stretches.begin(),
                                 stretches.end(),
                                 [](const auto& lhs, const auto& rhs)
                                 { return lhs.second - lhs.first > rhs.second - rhs.first; });
                for (const auto& [from, to] : stretches)
                {
                    if (to - from < minimumSpan || overlap.spans.size() == 3)
                        break;
                    overlap.spans.push_back(
                        {static_cast<double>(from) * hopSeconds, static_cast<double>(to) * hopSeconds});
                }
                found.push_back(std::move(overlap));
            }
        }
    }
    std::stable_sort(found.begin(),
                     found.end(),
                     [](const Overlap& lhs, const Overlap& rhs) { return lhs.share > rhs.share; });
    return found;
}

} // namespace daw::domain::mix
