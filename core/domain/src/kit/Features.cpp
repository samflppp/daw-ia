#include "daw/domain/kit/Features.h"

#include "daw/domain/sound/Pitch.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <vector>

#include "../dsp/Fft.h"

namespace daw::domain::kit
{
namespace
{

constexpr double floorDb = mix::silenceDb;

double toDb(double gain)
{
    return gain > 0.0 ? std::max(floorDb, 20.0 * std::log10(gain)) : floorDb;
}

double powerDb(double power)
{
    return power > 0.0 ? std::max(floorDb, 10.0 * std::log10(power)) : floorDb;
}

// The average power spectrum of the samples up to `length`, by frames of
// 2^14 under a Hann window, a quarter apart: fine enough for the low end
// (2.9 Hz a bin at 48 kHz), short sounds zero-padded into one frame.
constexpr std::size_t spectrumOrder = 14;

std::vector<double> powerSpectrum(const std::vector<float>& mono, std::size_t length)
{
    static const dsp::Fft fft{spectrumOrder};
    const auto size = fft.size();
    static const std::vector<double> hann = []
    {
        std::vector<double> window(std::size_t{1} << spectrumOrder);
        for (std::size_t index = 0; index < window.size(); ++index)
            window[index] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) /
                                                 static_cast<double>(window.size()));
        return window;
    }();

    std::vector<double> power(size / 2 + 1, 0.0);
    std::vector<std::complex<double>> data(size);
    std::size_t frames = 0;
    for (std::size_t start = 0; frames == 0 || start + size / 2 < length; start += size / 4)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            const auto at = start + index;
            data[index] = {at < length ? static_cast<double>(mono[at]) * hann[index] : 0.0, 0.0};
        }
        fft(data);
        for (std::size_t bin = 0; bin < power.size(); ++bin)
            power[bin] += std::norm(data[bin]);
        ++frames;
        if (start + size >= length)
            break;
    }
    for (auto& value : power)
        value /= static_cast<double>(frames);
    return power;
}

// The words of a path: lowercase, split on anything that is not a letter or
// a digit, and between letters and digits (« Kick808 » is « kick », « 808 »).
std::vector<std::string> wordsOf(std::string_view path)
{
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&]
    {
        if (!word.empty())
            words.push_back(word);
        word.clear();
    };
    for (const auto raw : path)
    {
        const auto character = static_cast<unsigned char>(raw);
        if (!std::isalnum(character))
        {
            flush();
            continue;
        }
        if (!word.empty() &&
            (std::isdigit(character) != 0) != (std::isdigit(static_cast<unsigned char>(word.back())) != 0))
            flush();
        word.push_back(static_cast<char>(std::tolower(character)));
    }
    flush();
    return words;
}

bool hasAny(const std::vector<std::string>& words, std::initializer_list<std::string_view> wanted)
{
    return std::any_of(words.begin(),
                       words.end(),
                       [&](const std::string& word)
                       { return std::find(wanted.begin(), wanted.end(), word) != wanted.end(); });
}

constexpr std::array<std::string_view, 7> roleNames{
    "kick", "caisse claire", "clap", "charley fermé", "charley ouvert", "percussion", "808"};
constexpr std::array<std::string_view, 7> roleTokens{
    "kick", "snare", "clap", "closedHat", "openHat", "percussion", "808"};

} // namespace

std::string_view nameOf(Role role) noexcept
{
    return roleNames[static_cast<std::size_t>(role)];
}

std::string_view tokenOf(Role role) noexcept
{
    return roleTokens[static_cast<std::size_t>(role)];
}

std::optional<Role> roleFromName(std::string_view name) noexcept
{
    for (std::size_t index = 0; index < roleTokens.size(); ++index)
        if (roleTokens[index] == name)
            return static_cast<Role>(index);
    return std::nullopt;
}

double lowBandCentre(std::size_t band) noexcept
{
    return lowFromHz * std::pow(2.0, (static_cast<double>(band) + 0.5) / 6.0);
}

Features measure(const float* left, const float* right, std::size_t count, double sampleRate)
{
    Features features;
    if (left == nullptr || count == 0 || sampleRate <= 0.0)
        return features;
    if (right == nullptr)
        right = left;
    features.seconds = static_cast<double>(count) / sampleRate;

    // What the mix measures of a stream: the bands, the crest, the width.
    mix::StreamAnalyser analyser{sampleRate};
    analyser.process(left, right, count);
    const auto stream = analyser.finish();
    features.bandsDb = stream.bandsDb;
    features.crestDb = stream.crestDb;
    features.width = stream.sideShare;

    std::vector<float> mono(count);
    for (std::size_t index = 0; index < count; ++index)
        mono[index] = 0.5f * (left[index] + right[index]);

    // A 1 ms envelope of the louder channel.
    const auto frame = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(sampleRate / 1000.0)));
    std::vector<double> envelope((count + frame - 1) / frame, 0.0);
    for (std::size_t index = 0; index < count; ++index)
    {
        auto& slot = envelope[index / frame];
        slot = std::max(
            {slot, static_cast<double>(std::abs(left[index])), static_cast<double>(std::abs(right[index]))});
    }
    const auto loudest = std::max_element(envelope.begin(), envelope.end());
    const auto peak = *loudest;
    features.peakDb = toDb(peak);
    if (peak <= 0.0)
        return features;
    const auto peakAt = static_cast<std::size_t>(loudest - envelope.begin());
    const auto secondsOf = [&](std::size_t frames)
    { return static_cast<double>(frames * frame) / sampleRate; };

    // The length, until it falls 60 dB under its peak for good.
    std::size_t last = peakAt;
    for (std::size_t index = envelope.size(); index-- > peakAt;)
        if (envelope[index] >= peak * 1e-3)
        {
            last = index;
            break;
        }
    features.lengthSeconds = secondsOf(last + 1);

    // The attack, 10 % to 90 % of the peak.
    std::size_t tenth = 0;
    while (tenth < peakAt && envelope[tenth] < 0.1 * peak)
        ++tenth;
    std::size_t ninth = tenth;
    while (ninth < peakAt && envelope[ninth] < 0.9 * peak)
        ++ninth;
    features.attackMs = secondsOf(ninth - tenth) * 1000.0;

    // The tail, -10 dB to -40 dB under the peak, after it.
    const auto down = [&](std::size_t from, double gain)
    {
        auto at = from;
        while (at < envelope.size() && envelope[at] > peak * gain)
            ++at;
        return at;
    };
    const auto ten = down(peakAt, std::pow(10.0, -10.0 / 20.0));
    const auto forty = down(ten, std::pow(10.0, -40.0 / 20.0));
    features.tailSeconds = secondsOf(forty - ten);

    // The RMS over its length.
    const auto length = std::min(count, (last + 1) * frame);
    double sum = 0.0;
    for (std::size_t index = 0; index < length; ++index)
        sum += 0.5 * (static_cast<double>(left[index]) * left[index] +
                      static_cast<double>(right[index]) * right[index]);
    features.rmsDb = powerDb(sum / static_cast<double>(std::max<std::size_t>(1, length)));

    // The spectrum: its centroid, the share above 5 kHz, the low profile.
    const auto power = powerSpectrum(mono, length);
    const auto binHz = sampleRate / static_cast<double>(std::size_t{1} << spectrumOrder);
    double total = 0.0;
    double weighted = 0.0;
    double high = 0.0;
    for (std::size_t bin = 1; bin < power.size(); ++bin)
    {
        const auto hz = static_cast<double>(bin) * binHz;
        total += power[bin];
        weighted += hz * power[bin];
        if (hz >= 5000.0)
            high += power[bin];
    }
    features.centroidHz = total > 0.0 ? weighted / total : 0.0;
    features.highShare = total > 0.0 ? high / total : 0.0;

    double lowest = -1.0;
    for (std::size_t band = 0; band < lowBands; ++band)
    {
        const auto from = lowFromHz * std::pow(2.0, static_cast<double>(band) / 6.0);
        const auto to = lowFromHz * std::pow(2.0, static_cast<double>(band + 1) / 6.0);
        auto first = static_cast<std::size_t>(std::ceil(from / binHz));
        auto lastBin = static_cast<std::size_t>(std::ceil(to / binHz));
        if (lastBin <= first)
            lastBin = first + 1;
        double bandPower = 0.0;
        for (auto bin = first; bin < lastBin && bin < power.size(); ++bin)
            bandPower += power[bin];
        features.lowProfileDb[band] = powerDb(bandPower);
        if (bandPower > lowest)
        {
            lowest = bandPower;
            features.lowPeakHz = lowBandCentre(band);
        }
    }

    // The pitch: YIN every 10 ms over 85 ms, where it is within 30 dB of its
    // peak — on the sound decimated to about 12 kHz: YIN's cost grows with
    // the window times the longest period, and the pitches looked for (30 Hz
    // to 1 kHz) need no more (S24: 32 ms a file at 48 kHz, measured on ten
    // thousand files).
    const auto factor = std::max<std::size_t>(1, static_cast<std::size_t>(sampleRate / 12000.0));
    const auto pitchRate = sampleRate / static_cast<double>(factor);
    std::vector<float> decimated(count / factor);
    for (std::size_t index = 0; index < decimated.size(); ++index)
    {
        float sumOf = 0.0f;
        for (std::size_t step = 0; step < factor; ++step)
            sumOf += mono[index * factor + step];
        decimated[index] = sumOf / static_cast<float>(factor);
    }
    const auto lengthDecimated = length / factor;
    const auto hop = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(0.01 * pitchRate)));
    const auto window = static_cast<std::size_t>(std::lround(0.085 * pitchRate));
    std::vector<double> heard; // MIDI, fractional; 0 for a hop without one
    for (std::size_t start = 0; start + window <= decimated.size() && start < lengthDecimated; start += hop)
    {
        double level = 0.0;
        for (auto index = start; index < start + hop; ++index)
            level = std::max(level, static_cast<double>(std::abs(decimated[index])));
        double midi = 0.0;
        if (level >= peak * std::pow(10.0, -30.0 / 20.0))
        {
            const auto hz = sound::fundamentalOf(
                decimated.data() + start, static_cast<int>(window), pitchRate, 30.0, 1000.0);
            if (hz > 0.0)
                midi = 69.0 + 12.0 * std::log2(hz / 440.0);
        }
        heard.push_back(midi);
    }

    // The body: consecutive hops within 10 cents of each other.
    std::vector<double> body;
    for (std::size_t index = 1; index < heard.size(); ++index)
        if (heard[index] > 0.0 && heard[index - 1] > 0.0 && std::abs(heard[index] - heard[index - 1]) < 0.1)
            body.push_back(heard[index]);
    if (body.size() >= 3)
    {
        auto sorted = body;
        std::nth_element(
            sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 2), sorted.end());
        const auto median = sorted[sorted.size() / 2];
        const auto note = std::lround(median);
        features.pitched = true;
        features.pitchHz = 440.0 * std::pow(2.0, (median - 69.0) / 12.0);
        features.pitchClass = static_cast<int>(((note % 12) + 12) % 12);
        features.cents = (median - static_cast<double>(note)) * 100.0;
        const auto first = std::find_if(heard.begin(), heard.end(), [](double midi) { return midi > 0.0; });
        features.glideSemitones = first != heard.end() ? *first - median : 0.0;
    }
    return features;
}

std::optional<Role> roleOf(std::string_view path, const Features& features)
{
    const auto words = wordsOf(path);
    std::optional<Role> named;
    bool open = hasAny(words, {"open", "openhat", "oh"});
    bool closed = hasAny(words, {"closed", "closedhat", "ch"});
    if (hasAny(words, {"kick", "kik", "kck", "bd", "bassdrum"}))
        named = Role::kick;
    else if (hasAny(words, {"snare", "snr", "sd"}))
        named = Role::snare;
    else if (hasAny(words, {"clap", "clp", "cp", "snap"}))
        named = Role::clap;
    else if (hasAny(words, {"hat", "hihat", "hh", "hats", "openhat", "closedhat", "oh", "ch"}))
        named = Role::closedHat;
    else if (hasAny(
                 words,
                 {"perc", "percussion", "shaker", "tamb", "tambourine", "conga", "bongo", "rim", "rimshot"}))
        named = Role::percussion;
    else if (hasAny(words, {"808", "sub", "subbass"}))
        named = Role::bass808;
    if (!named)
        return std::nullopt;

    // What the sound says back.
    switch (*named)
    {
    case Role::kick:
        return features.centroidHz < 2500.0 ? named : std::nullopt;
    case Role::bass808:
        return features.pitched && features.centroidHz < 2000.0 ? named : std::nullopt;
    case Role::snare:
    case Role::clap:
        return features.centroidHz > 500.0 ? named : std::nullopt;
    case Role::closedHat:
    case Role::openHat:
    {
        if (features.centroidHz < 3000.0)
            return std::nullopt;
        const bool longOne = features.lengthSeconds > 0.25;
        if ((open && !longOne) || (closed && longOne))
            return std::nullopt;
        return longOne ? Role::openHat : Role::closedHat;
    }
    case Role::percussion:
        return named;
    }
    return std::nullopt;
}

Value Features::toValue() const
{
    Value::Array bands;
    for (const auto band : bandsDb)
        bands.push_back(Value{std::round(band * 100.0) / 100.0});
    Value::Array low;
    for (const auto band : lowProfileDb)
        low.push_back(Value{std::round(band * 100.0) / 100.0});
    return Value::object({{"seconds", Value{seconds}},
                          {"length", Value{lengthSeconds}},
                          {"attackMs", Value{attackMs}},
                          {"tail", Value{tailSeconds}},
                          {"peakDb", Value{peakDb}},
                          {"rmsDb", Value{rmsDb}},
                          {"crestDb", Value{crestDb}},
                          {"width", Value{width}},
                          {"bands", Value::array(std::move(bands))},
                          {"centroidHz", Value{centroidHz}},
                          {"highShare", Value{highShare}},
                          {"pitched", Value{pitched}},
                          {"pitchHz", Value{pitchHz}},
                          {"pitchClass", Value{static_cast<std::int64_t>(pitchClass)}},
                          {"cents", Value{cents}},
                          {"glide", Value{glideSemitones}},
                          {"low", Value::array(std::move(low))},
                          {"lowPeakHz", Value{lowPeakHz}}});
}

Result<Features> Features::fromValue(const Value& value)
{
    Features features;
    const auto number = [&](std::string_view key, double& into) -> Result<void>
    {
        auto read = value.doubleAt(key);
        if (!read)
            return read.error();
        into = read.value();
        return {};
    };
    for (const auto& [key, into] :
         std::initializer_list<std::pair<std::string_view, double*>>{{"seconds", &features.seconds},
                                                                     {"length", &features.lengthSeconds},
                                                                     {"attackMs", &features.attackMs},
                                                                     {"tail", &features.tailSeconds},
                                                                     {"peakDb", &features.peakDb},
                                                                     {"rmsDb", &features.rmsDb},
                                                                     {"crestDb", &features.crestDb},
                                                                     {"width", &features.width},
                                                                     {"centroidHz", &features.centroidHz},
                                                                     {"highShare", &features.highShare},
                                                                     {"pitchHz", &features.pitchHz},
                                                                     {"cents", &features.cents},
                                                                     {"glide", &features.glideSemitones},
                                                                     {"lowPeakHz", &features.lowPeakHz}})
        if (auto read = number(key, *into); !read)
            return read.error();

    auto pitched = value.boolAt("pitched");
    if (!pitched)
        return pitched.error();
    features.pitched = pitched.value();
    auto pitchClass = value.intAt("pitchClass");
    if (!pitchClass)
        return pitchClass.error();
    features.pitchClass = static_cast<int>(pitchClass.value());

    const auto numbers = [&](std::string_view key, auto& into) -> Result<void>
    {
        const auto* list = value.find(key);
        const auto* items = list != nullptr ? list->asArray() : nullptr;
        if (items == nullptr || items->size() != into.size())
            return fail(ErrorCode::invalidArgument, "mesure de sample illisible : " + std::string{key});
        for (std::size_t index = 0; index < into.size(); ++index)
        {
            auto read = (*items)[index].asDouble();
            if (!read)
                return read.error();
            into[index] = read.value();
        }
        return {};
    };
    if (auto read = numbers("bands", features.bandsDb); !read)
        return read.error();
    if (auto read = numbers("low", features.lowProfileDb); !read)
        return read.error();
    return features;
}

} // namespace daw::domain::kit
