#include "daw/domain/mix/Measurement.h"

#include <cmath>
#include <functional>
#include <numbers>
#include <random>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain::mix;

namespace
{

constexpr double rate = 48000.0;

struct Segment
{
    double seconds;
    double levelDb; // peak amplitude of the sine, dBFS
};

// The 1 kHz stereo sine of EBU Tech 3341, in segments of given levels. Fed in
// blocks of 512, as a render hands them over.
StreamMeasure sineSegments(const std::vector<Segment>& segments, double frequency = 1000.0)
{
    StreamAnalyser analyser{rate};
    std::vector<float> block(512);
    double phase = 0.0;
    for (const auto& segment : segments)
    {
        const auto amplitude = std::pow(10.0, segment.levelDb / 20.0);
        auto remaining = static_cast<std::size_t>(std::lround(segment.seconds * rate));
        while (remaining > 0)
        {
            const auto count = std::min<std::size_t>(remaining, block.size());
            for (std::size_t index = 0; index < count; ++index)
            {
                block[index] = static_cast<float>(amplitude * std::sin(phase));
                phase += 2.0 * std::numbers::pi * frequency / rate;
            }
            analyser.process(block.data(), block.data(), count);
            remaining -= count;
        }
    }
    return analyser.finish();
}

StreamMeasure stereo(double seconds, const std::function<std::pair<float, float>(std::size_t)>& sample)
{
    StreamAnalyser analyser{rate};
    const auto total = static_cast<std::size_t>(seconds * rate);
    std::vector<float> left(total), right(total);
    for (std::size_t index = 0; index < total; ++index)
        std::tie(left[index], right[index]) = sample(index);
    analyser.process(left.data(), right.data(), total);
    return analyser.finish();
}

} // namespace

TEST_CASE("loudness: the stationary sines of EBU Tech 3341, cases 1 and 2")
{
    CHECK(sineSegments({{20.0, -23.0}}).integratedLufs == doctest::Approx(-23.0).epsilon(0.1 / 23.0));
    CHECK(sineSegments({{20.0, -33.0}}).integratedLufs == doctest::Approx(-33.0).epsilon(0.1 / 33.0));
}

TEST_CASE("loudness: the gated programmes of EBU Tech 3341, cases 3, 4 and 5")
{
    const auto three = sineSegments({{10.0, -36.0}, {60.0, -23.0}, {10.0, -36.0}});
    const auto four =
        sineSegments({{10.0, -72.0}, {10.0, -36.0}, {60.0, -23.0}, {10.0, -36.0}, {10.0, -72.0}});
    const auto five = sineSegments({{20.0, -26.0}, {20.1, -20.0}, {20.0, -26.0}});
    MESSAGE("case 3: " << three.integratedLufs << ", case 4: " << four.integratedLufs
                       << ", case 5: " << five.integratedLufs);
    CHECK(std::abs(three.integratedLufs + 23.0) <= 0.1);
    CHECK(std::abs(four.integratedLufs + 23.0) <= 0.1);
    CHECK(std::abs(five.integratedLufs + 23.0) <= 0.1);
}

TEST_CASE("loudness: short-term and momentary maxima of a step")
{
    const auto step = sineSegments({{10.0, -30.0}, {10.0, -20.0}});
    CHECK(step.shortTermMaxLufs == doctest::Approx(-20.0).epsilon(0.01));
    CHECK(step.momentaryMaxLufs == doctest::Approx(-20.0).epsilon(0.01));
}

TEST_CASE("true peak: a sine at a quarter of the rate, sampled 45 degrees off its crest")
{
    // Every sample is at 0.7071 of the crest: the sample peak reads -3 dB, the
    // signal between the samples reaches 0 dB.
    const auto measure =
        stereo(2.0,
               [](std::size_t index)
               {
                   const auto value = static_cast<float>(std::sin(
                       std::numbers::pi / 2.0 * static_cast<double>(index) + std::numbers::pi / 4.0));
                   return std::pair{value, value};
               });
    MESSAGE("sample peak " << measure.samplePeakDb << " dB, true peak " << measure.truePeakDb << " dBTP");
    CHECK(measure.samplePeakDb == doctest::Approx(-3.01).epsilon(0.01));
    CHECK(measure.truePeakDb > -0.4);
    CHECK(measure.truePeakDb < 0.3);
}

TEST_CASE("bands: a 1 kHz sine sits in the 1 kHz octave, at its mean square")
{
    const auto measure = sineSegments({{5.0, 0.0}});
    CHECK(measure.bandsDb[5] == doctest::Approx(-3.01).epsilon(0.03));
    for (std::size_t band = 0; band < bandCount; ++band)
    {
        if (band != 5)
            CHECK(measure.bandsDb[band] < measure.bandsDb[5] - 40.0);
    }
}

TEST_CASE("bands: white noise climbs 3 dB an octave")
{
    std::mt19937 random{3};
    std::normal_distribution<float> gauss{0.0f, 0.1f};
    const auto measure = stereo(10.0,
                                [&](std::size_t)
                                {
                                    const auto value = gauss(random);
                                    return std::pair{value, value};
                                });
    for (std::size_t band = 3; band + 1 < bandCount - 1; ++band)
        CHECK(measure.bandsDb[band + 1] - measure.bandsDb[band] == doctest::Approx(3.01).epsilon(0.1));
}

TEST_CASE("stereo width: mono, opposite, and two unrelated channels")
{
    std::mt19937 random{5};
    std::normal_distribution<float> gauss{0.0f, 0.1f};
    const auto mono = stereo(2.0,
                             [&](std::size_t)
                             {
                                 const auto value = gauss(random);
                                 return std::pair{value, value};
                             });
    const auto opposite = stereo(2.0,
                                 [&](std::size_t)
                                 {
                                     const auto value = gauss(random);
                                     return std::pair{value, -value};
                                 });
    const auto wide = stereo(2.0, [&](std::size_t) { return std::pair{gauss(random), gauss(random)}; });
    CHECK(mono.correlation == doctest::Approx(1.0));
    CHECK(mono.sideShare == doctest::Approx(0.0));
    CHECK(opposite.correlation == doctest::Approx(-1.0));
    CHECK(opposite.sideShare == doctest::Approx(1.0));
    CHECK(std::abs(wide.correlation) < 0.05);
    CHECK(wide.sideShare == doctest::Approx(0.5).epsilon(0.05));
}

TEST_CASE("crest factor: a steady sine has none, a hit over a quiet tone has some")
{
    const auto steady = sineSegments({{4.0, -10.0}});
    CHECK(std::abs(steady.crestDb) < 0.2);
    const auto hits = stereo(4.0,
                             [](std::size_t index)
                             {
                                 const auto time = static_cast<double>(index) / rate;
                                 const auto loud = std::fmod(time, 0.5) < 0.05;
                                 const auto value = static_cast<float>(
                                     (loud ? 0.9 : 0.1) * std::sin(2.0 * std::numbers::pi * 220.0 * time));
                                 return std::pair{value, value};
                             });
    MESSAGE("crest of hits: " << hits.crestDb << " dB");
    CHECK(hits.crestDb > 8.0);
}

TEST_CASE("activity: silence then a tone plays half the time")
{
    const auto half = sineSegments({{10.0, -200.0}, {10.0, -20.0}});
    CHECK(half.activeShare == doctest::Approx(0.5).epsilon(0.05));
}

TEST_CASE("masking: two lows at the same level overlap in their octave, a high does not")
{
    const auto kick = sineSegments({{8.0, -12.0}}, 55.0);
    const auto bass = sineSegments({{8.0, -14.0}}, 70.0);
    const auto voice = sineSegments({{8.0, -12.0}}, 2000.0);
    const auto found = overlaps({kick, bass, voice});
    REQUIRE_FALSE(found.empty());
    CHECK(found.front().first == 0);
    CHECK(found.front().second == 1);
    CHECK(found.front().band == 1); // 63 Hz
    CHECK(found.front().share > 0.9);
    REQUIRE_FALSE(found.front().spans.empty());
    CHECK(found.front().spans.front().toSeconds - found.front().spans.front().fromSeconds > 7.0);
    for (const auto& overlap : found)
        CHECK(overlap.second != 2);

    // Ten decibels apart, they no longer mask each other.
    const auto quiet = sineSegments({{8.0, -24.0}}, 70.0);
    CHECK(overlaps({kick, quiet}).empty());
}

TEST_CASE("gained: a fader moves every level and nothing else, and ends a masking")
{
    const auto kick = sineSegments({{8.0, -12.0}}, 55.0);
    const auto bass = sineSegments({{8.0, -14.0}}, 70.0);
    const auto faded = gained(bass, -10.0);
    CHECK(faded.integratedLufs == doctest::Approx(bass.integratedLufs - 10.0).epsilon(0.001));
    CHECK(faded.truePeakDb == doctest::Approx(bass.truePeakDb - 10.0).epsilon(0.001));
    CHECK(faded.bandsDb[1] == doctest::Approx(bass.bandsDb[1] - 10.0).epsilon(0.001));
    CHECK(faded.crestDb == doctest::Approx(bass.crestDb).epsilon(0.001));
    CHECK(faded.correlation == doctest::Approx(bass.correlation).epsilon(0.001));
    CHECK(faded.activeShare == doctest::Approx(bass.activeShare).epsilon(0.001));

    // The same two strips: masking at their faders' unity, not 10 dB apart.
    CHECK_FALSE(overlaps({kick, bass}).empty());
    CHECK(overlaps({kick, faded}).empty());
}

TEST_CASE("combine: a track's two roads add their powers, and silence adds nothing")
{
    std::mt19937 first{11};
    std::mt19937 second{12};
    std::normal_distribution<float> gauss{0.0f, 0.05f};
    const auto a = stereo(6.0,
                          [&](std::size_t)
                          {
                              const auto value = gauss(first);
                              return std::pair{value, value};
                          });
    const auto b = stereo(6.0,
                          [&](std::size_t)
                          {
                              const auto value = gauss(second);
                              return std::pair{value, value};
                          });
    const auto both = combine(a, b);
    CHECK(both.integratedLufs - a.integratedLufs == doctest::Approx(3.01).epsilon(0.05));
    CHECK(both.bandsDb[6] - a.bandsDb[6] == doctest::Approx(3.01).epsilon(0.1));

    const auto silent = sineSegments({{6.0, -200.0}});
    const auto alone = combine(a, silent);
    CHECK(alone.integratedLufs == doctest::Approx(a.integratedLufs).epsilon(0.001));
    CHECK(alone.truePeakDb == doctest::Approx(a.truePeakDb));
}
