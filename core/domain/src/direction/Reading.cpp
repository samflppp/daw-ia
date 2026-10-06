#include "daw/domain/direction/Reading.h"

#include "daw/domain/mix/Decision.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>

#include "../dsp/Fft.h"

namespace daw::domain::direction
{
namespace
{

double toDb(double meanSquare)
{
    return meanSquare > 1e-12 ? 10.0 * std::log10(meanSquare) : mix::silenceDb;
}

std::vector<double> hann(std::size_t size)
{
    std::vector<double> window(size);
    for (std::size_t index = 0; index < size; ++index)
        window[index] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) /
                                             static_cast<double>(size));
    return window;
}

// --- tempo -----------------------------------------------------------------
//
// The onset strength: the positive change of the log spectrum from one hop to
// the next, summed over the bins (spectral flux), every 256 samples (5.8 ms
// at 44.1 kHz). Its autocorrelation peaks at the beat's period; a log-normal
// weight around 120 BPM chooses between a period and its double, the way a
// listener taps.

constexpr std::size_t tempoOrder = 10; // 1024 points
constexpr std::size_t tempoHop = 256;
constexpr double tempoMinBpm = 60.0;
constexpr double tempoMaxBpm = 200.0;

std::vector<double> onsetStrength(const std::vector<float>& mono, double rate, std::size_t& hop)
{
    static const dsp::Fft fft{tempoOrder};
    const auto size = fft.size();
    const auto window = hann(size);
    hop = tempoHop;

    std::vector<double> envelope;
    std::vector<double> previous(size / 2, 0.0);
    std::vector<std::complex<double>> frame(size);
    for (std::size_t start = 0; start + size <= mono.size(); start += hop)
    {
        for (std::size_t index = 0; index < size; ++index)
            frame[index] = {static_cast<double>(mono[start + index]) * window[index], 0.0};
        fft(frame);
        // Below 1.5 kHz: the kick and the snare carry the beat; hats on the
        // eighths would double it.
        double flux = 0.0;
        const auto topBin =
            std::min(size / 2, static_cast<std::size_t>(1500.0 * static_cast<double>(size) / rate));
        for (std::size_t bin = 1; bin < topBin; ++bin)
        {
            const auto magnitude = std::log1p(1000.0 * std::abs(frame[bin]));
            flux += std::max(0.0, magnitude - previous[bin]);
            previous[bin] = magnitude;
        }
        envelope.push_back(flux);
    }

    // Without its slow part: what changes at the beat, not the level.
    const std::size_t half = 16;
    std::vector<double> detrended(envelope.size(), 0.0);
    for (std::size_t index = 0; index < envelope.size(); ++index)
    {
        const auto from = index > half ? index - half : 0;
        const auto to = std::min(envelope.size(), index + half + 1);
        const auto mean = std::accumulate(envelope.begin() + static_cast<std::ptrdiff_t>(from),
                                          envelope.begin() + static_cast<std::ptrdiff_t>(to),
                                          0.0) /
                          static_cast<double>(to - from);
        detrended[index] = std::max(0.0, envelope[index] - mean);
    }
    return detrended;
}

// --- key -------------------------------------------------------------------
//
// A chroma: the energy of each pitch class, from 55 Hz to 2 kHz, over the
// song; correlated with the Krumhansl-Kessler profiles of the 24 keys.

constexpr std::size_t keyOrder = 14; // 16384 points: 2.7 Hz a bin at 44.1 kHz, a semitone at 55 Hz is 3.3
constexpr std::array<double, 12> majorProfile{
    6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
constexpr std::array<double, 12> minorProfile{
    6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double correlation(const std::array<double, 12>& a, const std::array<double, 12>& b)
{
    const auto meanA = std::accumulate(a.begin(), a.end(), 0.0) / 12.0;
    const auto meanB = std::accumulate(b.begin(), b.end(), 0.0) / 12.0;
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (std::size_t index = 0; index < 12; ++index)
    {
        ab += (a[index] - meanA) * (b[index] - meanB);
        aa += (a[index] - meanA) * (a[index] - meanA);
        bb += (b[index] - meanB) * (b[index] - meanB);
    }
    return aa > 0.0 && bb > 0.0 ? ab / std::sqrt(aa * bb) : 0.0;
}

// --- loudness per window ------------------------------------------------------

double meanSquare(const Stereo& stereo, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    to = std::min(to, stereo.left.size());
    for (std::size_t index = from; index < to; ++index)
        sum += 0.5 * (static_cast<double>(stereo.left[index]) * stereo.left[index] +
                      static_cast<double>(stereo.right[index]) * stereo.right[index]);
    return to > from ? sum / static_cast<double>(to - from) : 0.0;
}

mix::StreamMeasure measure(const Stereo& stereo, double rate)
{
    mix::StreamAnalyser analyser{rate};
    analyser.process(stereo.left.data(), stereo.right.data(), stereo.left.size());
    return analyser.finish();
}

} // namespace

TempoEstimate estimateTempo(const std::vector<float>& mono, double rate)
{
    std::size_t hop = tempoHop;
    const auto onsets = onsetStrength(mono, rate, hop);
    const auto hopsPerSecond = rate / static_cast<double>(hop);
    const auto shortest = static_cast<std::size_t>(std::floor(60.0 * hopsPerSecond / tempoMaxBpm));
    const auto longest = static_cast<std::size_t>(std::ceil(60.0 * hopsPerSecond / tempoMinBpm));
    if (onsets.size() < 4 * longest)
        return {};

    std::vector<double> score(longest + 2, 0.0);
    double energy = 0.0;
    for (const auto value : onsets)
        energy += value * value;
    if (energy <= 0.0)
        return {};

    for (std::size_t lag = shortest; lag <= longest + 1; ++lag)
    {
        double sum = 0.0;
        for (std::size_t index = lag; index < onsets.size(); ++index)
            sum += onsets[index] * onsets[index - lag];
        score[lag] = sum / energy;
    }

    // The listener's prior: log-normal around 120 BPM, an octave wide.
    std::size_t best = shortest;
    double bestWeighted = -1.0;
    for (std::size_t lag = shortest; lag <= longest; ++lag)
    {
        const auto bpm = 60.0 * hopsPerSecond / static_cast<double>(lag);
        const auto octaves = std::log2(bpm / 120.0);
        const auto weighted = score[lag] * std::exp(-0.5 * octaves * octaves);
        if (weighted > bestWeighted)
        {
            bestWeighted = weighted;
            best = lag;
        }
    }

    // Between two hops: a parabola through the peak and its neighbours.
    double lag = static_cast<double>(best);
    if (best > shortest && best < longest)
    {
        const auto left = score[best - 1];
        const auto centre = score[best];
        const auto right = score[best + 1];
        const auto denominator = left - 2.0 * centre + right;
        if (denominator < 0.0)
            lag += 0.5 * (left - right) / denominator;
    }

    // How periodic the onsets are at that period: their normalised
    // autocorrelation, near 1 for a steady beat, near 0 for noise.
    auto confidence = std::clamp(score[best], 0.0, 1.0);

    // And whether anything is struck. A held low chord makes the spectrum
    // waver (two partials leak into the same bins) and its flux is periodic
    // too, without one attack: the strongest onsets of a beat stand thirty
    // times over their mean, a held chord's seven, noise's six.
    std::vector<double> sorted(onsets);
    const auto top = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() * 99 / 100);
    std::nth_element(sorted.begin(), top, sorted.end());
    const auto mean = std::accumulate(onsets.begin(), onsets.end(), 0.0) / static_cast<double>(onsets.size());
    if (mean <= 0.0 || *top / mean < attackContrast)
        confidence = 0.0;
    return {60.0 * hopsPerSecond / lag, confidence};
}

KeyEstimate estimateKey(const std::vector<float>& mono, double rate)
{
    static const dsp::Fft fft{keyOrder};
    const auto size = fft.size();
    const auto window = hann(size);

    std::array<double, 12> chroma{};
    std::vector<std::complex<double>> frame(size);
    for (std::size_t start = 0; start + size <= mono.size(); start += size / 2)
    {
        for (std::size_t index = 0; index < size; ++index)
            frame[index] = {static_cast<double>(mono[start + index]) * window[index], 0.0};
        fft(frame);
        for (std::size_t bin = 1; bin < size / 2; ++bin)
        {
            const auto frequency = static_cast<double>(bin) * rate / static_cast<double>(size);
            if (frequency < 55.0 || frequency > 2000.0)
                continue;
            const auto midi = 69.0 + 12.0 * std::log2(frequency / 440.0);
            const auto nearest = std::round(midi);
            if (std::abs(midi - nearest) > 0.3) // between two semitones: neither
                continue;
            const auto pitchClass =
                static_cast<std::size_t>((static_cast<long long>(nearest) % 12 + 12) % 12);
            chroma[pitchClass] += std::norm(frame[bin]);
        }
    }
    // Compressed, so that one loud note does not make the key.
    for (auto& value : chroma)
        value = std::sqrt(value);

    struct Scored
    {
        generation::Key key;
        double r{0.0};
    };
    std::vector<Scored> scored;
    for (int tonic = 0; tonic < 12; ++tonic)
    {
        for (const auto mode : {generation::Mode::major, generation::Mode::minor})
        {
            const auto& profile = mode == generation::Mode::major ? majorProfile : minorProfile;
            std::array<double, 12> rotated{};
            for (std::size_t index = 0; index < 12; ++index)
                rotated[(index + static_cast<std::size_t>(tonic)) % 12] = profile[index];
            scored.push_back({generation::Key{tonic, mode}, correlation(chroma, rotated)});
        }
    }
    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.r > b.r; });

    // Noise, or no harmony: no key fits well. Else, how far the best stands
    // above the next, which is often its relative (A minor and C major share
    // their notes; only the weight of the tonic tells them apart).
    KeyEstimate estimate;
    estimate.key = scored[0].key;
    estimate.second = scored[1].key;
    estimate.fit = scored[0].r;
    estimate.confidence = scored[0].r < keyFitFloor ? 0.0 : std::clamp(scored[0].r - scored[1].r, 0.0, 1.0);
    return estimate;
}

Reading read(const std::map<std::string, Stereo>& stems, double rate, std::string name, std::string digest)
{
    Reading reading;
    reading.name = std::move(name);
    reading.digest = std::move(digest);

    std::size_t frames = 0;
    for (const auto& [stem, audio] : stems)
        frames = std::max(frames, audio.left.size());
    reading.seconds = static_cast<double>(frames) / rate;
    if (frames == 0)
        return reading;

    const auto stemOf = [&stems, frames](const char* stemName) -> Stereo
    {
        auto found = stems.find(stemName);
        if (found != stems.end())
            return found->second;
        return Stereo{std::vector<float>(frames, 0.0f), std::vector<float>(frames, 0.0f)};
    };
    const auto mono = [frames](const std::vector<const Stereo*>& parts)
    {
        std::vector<float> out(frames, 0.0f);
        for (const auto* part : parts)
            for (std::size_t index = 0; index < std::min(frames, part->left.size()); ++index)
                out[index] += 0.5f * (part->left[index] + part->right[index]);
        return out;
    };

    const auto vocals = stemOf("vocals");
    const auto drums = stemOf("drums");
    const auto bass = stemOf("bass");
    const auto other = stemOf("other");
    Stereo whole{std::vector<float>(frames, 0.0f), std::vector<float>(frames, 0.0f)};
    for (const auto* part : {&vocals, &drums, &bass, &other})
        for (std::size_t index = 0; index < frames; ++index)
        {
            whole.left[index] += part->left[index];
            whole.right[index] += part->right[index];
        }

    // The tempo from the drums when they play, else from the whole.
    const auto drumsLevel = toDb(meanSquare(drums, 0, frames));
    const auto tempo = estimateTempo(drumsLevel > -50.0 ? mono({&drums}) : mono({&whole}), rate);
    reading.bpmConfidence = tempo.confidence;
    if (tempo.confidence >= tempoTrusted)
        reading.bpm = tempo.bpm;

    // The key from what carries pitches: never the drums.
    const auto key = estimateKey(mono({&bass, &other, &vocals}), rate);
    reading.keyConfidence = key.confidence;
    if (key.fit >= keyFitFloor)
        reading.keyCandidates = {key.key, key.second};
    if (key.confidence >= keyTrusted)
        reading.key = key.key;

    // The whole, as the mix measures a master.
    const auto measured = measure(whole, rate);
    reading.tilt = mix::tiltOf(measured);
    reading.crestDb = measured.crestDb;
    reading.sideShare = measured.sideShare;
    reading.loudnessLufs = measured.integratedLufs;

    std::array<double, 4> stemPeakDb{};
    const std::array<const Stereo*, 4> parts{&vocals, &drums, &bass, &other};
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        const auto stemMeasure = measure(*parts[index], rate);
        StemReading stem;
        stem.loudnessLufs = stemMeasure.integratedLufs;
        stem.balanceDb = stemMeasure.integratedLufs - measured.integratedLufs;
        stem.activeShare = stemMeasure.activeShare;
        reading.stems.emplace(stemNames[index], stem);
        stemPeakDb[index] = mix::silenceDb;
    }

    // --- sections: a bar at a time (two seconds without a tempo), what plays
    // and how loud; a new section where that changes, four bars at least.
    const auto barSeconds = reading.bpm ? 4.0 * 60.0 / *reading.bpm : 2.0;
    const auto barFrames = static_cast<std::size_t>(barSeconds * rate);
    struct Bar
    {
        double loudnessDb{0.0};
        std::array<double, 4> levelDb{};
    };
    std::vector<Bar> bars;
    for (std::size_t from = 0; from + barFrames / 2 <= frames; from += barFrames)
    {
        Bar bar;
        bar.loudnessDb = toDb(meanSquare(whole, from, from + barFrames));
        for (std::size_t index = 0; index < parts.size(); ++index)
        {
            bar.levelDb[index] = toDb(meanSquare(*parts[index], from, from + barFrames));
            stemPeakDb[index] = std::max(stemPeakDb[index], bar.levelDb[index]);
        }
        bars.push_back(bar);
    }

    // A stem plays in a bar when it is within 20 dB of its loudest bar.
    const auto plays = [&stemPeakDb](const Bar& bar)
    {
        std::array<double, 4> out{};
        for (std::size_t index = 0; index < 4; ++index)
            out[index] =
                stemPeakDb[index] > -60.0 && bar.levelDb[index] > stemPeakDb[index] - 20.0 ? 1.0 : 0.0;
        return out;
    };
    const auto distance = [&plays](const Bar& a, const Bar& b)
    {
        const auto pa = plays(a);
        const auto pb = plays(b);
        double d = std::abs(a.loudnessDb - b.loudnessDb) / 6.0;
        for (std::size_t index = 0; index < 4; ++index)
            d += std::abs(pa[index] - pb[index]);
        return d;
    };

    constexpr std::size_t minimumBars = 4;
    std::vector<std::size_t> starts{0};
    for (std::size_t index = 1; index < bars.size(); ++index)
    {
        if (index - starts.back() < minimumBars)
            continue;
        if (distance(bars[index], bars[index - 1]) >= 1.0)
            starts.push_back(index);
    }

    std::vector<std::array<double, 5>> signatures; // per section: activity of each stem, loudness
    for (std::size_t s = 0; s < starts.size(); ++s)
    {
        const auto first = starts[s];
        const auto last = s + 1 < starts.size() ? starts[s + 1] : bars.size();
        Section section;
        section.fromSeconds = static_cast<double>(first) * barSeconds;
        section.toSeconds = std::min(reading.seconds, static_cast<double>(last) * barSeconds);
        double loud = 0.0;
        for (std::size_t bar = first; bar < last; ++bar)
        {
            loud += std::pow(10.0, bars[bar].loudnessDb / 10.0);
            const auto played = plays(bars[bar]);
            for (std::size_t index = 0; index < 4; ++index)
                section.activity[index] += played[index] / static_cast<double>(last - first);
        }
        section.loudnessDb = toDb(loud / static_cast<double>(last - first));

        const std::array<double, 5> signature{section.activity[0],
                                              section.activity[1],
                                              section.activity[2],
                                              section.activity[3],
                                              section.loudnessDb / 6.0};
        section.label = static_cast<char>('A' + signatures.size());
        for (std::size_t earlier = 0; earlier < signatures.size(); ++earlier)
        {
            double d = 0.0;
            for (std::size_t k = 0; k < 5; ++k)
                d += std::abs(signature[k] - signatures[earlier][k]);
            if (d < 0.75)
            {
                section.label = reading.sections[earlier].label;
                break;
            }
        }
        signatures.push_back(signature);
        reading.sections.push_back(section);
    }
    return reading;
}

// --- serialisation --------------------------------------------------------

Value Reading::toValue() const
{
    Value::Array sectionValues;
    for (const auto& section : sections)
    {
        Value::Array activity;
        for (const auto value : section.activity)
            activity.push_back(Value{value});
        sectionValues.push_back(Value::object({{"from", Value{section.fromSeconds}},
                                               {"to", Value{section.toSeconds}},
                                               {"label", Value{std::string(1, section.label)}},
                                               {"loudnessDb", Value{section.loudnessDb}},
                                               {"activity", Value::array(std::move(activity))}}));
    }
    Value::Object stemValues;
    for (const auto& [stem, reading] : stems)
        stemValues.emplace_back(stem,
                                Value::object({{"loudnessLufs", Value{reading.loudnessLufs}},
                                               {"balanceDb", Value{reading.balanceDb}},
                                               {"activeShare", Value{reading.activeShare}}}));
    Value::Array tiltValues;
    for (const auto value : tilt)
        tiltValues.push_back(Value{value});

    Value::Object members{{"name", Value{name}},
                          {"digest", Value{digest}},
                          {"seconds", Value{seconds}},
                          {"bpmConfidence", Value{bpmConfidence}},
                          {"keyConfidence", Value{keyConfidence}},
                          {"sections", Value::array(std::move(sectionValues))},
                          {"stems", Value::object(std::move(stemValues))},
                          {"tilt", Value::array(std::move(tiltValues))},
                          {"crestDb", Value{crestDb}},
                          {"sideShare", Value{sideShare}},
                          {"loudnessLufs", Value{loudnessLufs}}};
    if (bpm)
        members.emplace_back("bpm", Value{*bpm});
    if (!keyCandidates.empty())
    {
        Value::Array candidates;
        for (const auto& candidate : keyCandidates)
            candidates.push_back(Value::object(
                {{"tonic", Value{candidate.tonic}},
                 {"mode",
                  Value{std::string{candidate.mode == generation::Mode::major ? "major" : "minor"}}}}));
        members.emplace_back("keyCandidates", Value::array(std::move(candidates)));
    }
    if (key)
        members.emplace_back(
            "key",
            Value::object(
                {{"tonic", Value{key->tonic}},
                 {"mode", Value{std::string{key->mode == generation::Mode::major ? "major" : "minor"}}}}));
    auto out = Value::object(std::move(members));
    return out;
}

Result<Reading> Reading::fromValue(const Value& value)
{
    Reading reading;
    const auto text = [&value](const char* key) -> Result<std::string> { return value.stringAt(key); };
    const auto number = [&value](const char* key, double fallback)
    {
        const auto found = value.doubleAt(key);
        return found ? found.value() : fallback;
    };

    auto name = text("name");
    auto digest = text("digest");
    if (!name || !digest)
        return fail(ErrorCode::invalidPayload, "a reading needs a name and a digest");
    reading.name = name.value();
    reading.digest = digest.value();
    reading.seconds = number("seconds", 0.0);
    reading.bpmConfidence = number("bpmConfidence", 0.0);
    reading.keyConfidence = number("keyConfidence", 0.0);
    if (const auto bpm = value.doubleAt("bpm"); bpm)
        reading.bpm = bpm.value();
    const auto keyOf = [](const Value& item) -> Result<generation::Key>
    {
        const auto tonic = item.intAt("tonic");
        const auto mode = item.stringAt("mode");
        if (!tonic || !mode || tonic.value() < 0 || tonic.value() > 11 ||
            (mode.value() != "major" && mode.value() != "minor"))
            return fail(ErrorCode::invalidPayload, "a reading's key is a tonic 0-11 and a mode");
        return generation::Key{static_cast<int>(tonic.value()),
                               mode.value() == "major" ? generation::Mode::major : generation::Mode::minor};
    };
    if (const auto* key = value.find("key"); key != nullptr && !key->isNull())
    {
        auto read = keyOf(*key);
        if (!read)
            return read.error();
        reading.key = read.value();
    }
    if (const auto* candidates = value.find("keyCandidates");
        candidates != nullptr && candidates->asArray() != nullptr)
    {
        for (const auto& item : *candidates->asArray())
        {
            auto read = keyOf(item);
            if (!read)
                return read.error();
            reading.keyCandidates.push_back(read.value());
        }
    }
    if (const auto* sections = value.find("sections"); sections != nullptr && sections->asArray() != nullptr)
    {
        for (const auto& item : *sections->asArray())
        {
            Section section;
            section.fromSeconds = item.doubleAt("from") ? item.doubleAt("from").value() : 0.0;
            section.toSeconds = item.doubleAt("to") ? item.doubleAt("to").value() : 0.0;
            const auto label = item.stringAt("label");
            section.label = label && !label.value().empty() ? label.value().front() : 'A';
            section.loudnessDb = item.doubleAt("loudnessDb") ? item.doubleAt("loudnessDb").value() : 0.0;
            if (const auto* activity = item.find("activity");
                activity != nullptr && activity->asArray() != nullptr)
            {
                for (std::size_t index = 0; index < std::min<std::size_t>(4, activity->asArray()->size());
                     ++index)
                {
                    const auto v = (*activity->asArray())[index].asDouble();
                    section.activity[index] = v ? v.value() : 0.0;
                }
            }
            reading.sections.push_back(section);
        }
    }
    if (const auto* stems = value.find("stems"); stems != nullptr && stems->asObject() != nullptr)
    {
        for (const auto& [stem, item] : *stems->asObject())
        {
            StemReading read;
            read.loudnessLufs =
                item.doubleAt("loudnessLufs") ? item.doubleAt("loudnessLufs").value() : mix::silenceDb;
            read.balanceDb = item.doubleAt("balanceDb") ? item.doubleAt("balanceDb").value() : 0.0;
            read.activeShare = item.doubleAt("activeShare") ? item.doubleAt("activeShare").value() : 0.0;
            reading.stems.emplace(stem, read);
        }
    }
    if (const auto* tilt = value.find("tilt"); tilt != nullptr && tilt->asArray() != nullptr)
    {
        for (std::size_t index = 0; index < std::min(reading.tilt.size(), tilt->asArray()->size()); ++index)
        {
            const auto v = (*tilt->asArray())[index].asDouble();
            reading.tilt[index] = v ? v.value() : 0.0;
        }
    }
    reading.crestDb = number("crestDb", 0.0);
    reading.sideShare = number("sideShare", 0.0);
    reading.loudnessLufs = number("loudnessLufs", mix::silenceDb);
    return reading;
}

} // namespace daw::domain::direction
