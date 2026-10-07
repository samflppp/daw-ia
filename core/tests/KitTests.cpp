#include "daw/domain/kit/Choice.h"
#include "daw/domain/kit/Features.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::kit;

// The kit (S24): what a sample is measured to be, on sounds whose answer is
// known, and the kit chosen from a library built for the purpose.

namespace
{

constexpr double rate = 48000.0;

std::size_t samplesOf(double seconds)
{
    return static_cast<std::size_t>(seconds * rate);
}

// A tone gliding from `fromHz` to `toHz` in `glideSeconds`, then holding,
// fading by `decayDbPerSecond`.
std::vector<float>
tone(double fromHz, double toHz, double glideSeconds, double seconds, double decayDbPerSecond)
{
    std::vector<float> samples(samplesOf(seconds));
    double phase = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto t = static_cast<double>(index) / rate;
        const auto hz = t < glideSeconds ? fromHz * std::pow(toHz / fromHz, t / glideSeconds) : toHz;
        phase += 2.0 * std::numbers::pi * hz / rate;
        const auto gain = 0.8 * std::pow(10.0, -decayDbPerSecond * t / 20.0);
        samples[index] = static_cast<float>(gain * std::sin(phase));
    }
    return samples;
}

std::vector<float> noise(double seconds, double decayDbPerSecond, unsigned seed, bool highPassed)
{
    std::vector<float> samples(samplesOf(seconds));
    std::mt19937 random{seed};
    std::uniform_real_distribution<float> uniform{-0.5f, 0.5f};
    float previous = 0.0f;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto t = static_cast<double>(index) / rate;
        const auto value = uniform(random);
        // A first difference: little left under a few kHz.
        const auto shaped = highPassed ? value - previous : value;
        previous = value;
        samples[index] = static_cast<float>(shaped * std::pow(10.0, -decayDbPerSecond * t / 20.0));
    }
    return samples;
}

Features of(const std::vector<float>& mono)
{
    return measure(mono.data(), mono.data(), mono.size(), rate);
}

} // namespace

TEST_CASE(
    "An 808 in A that glides in from two semitones above: its pitch is the body's, the glide said apart")
{
    // A1 is 55 Hz; it starts at B1 and falls to A1 in 60 ms.
    const auto sound = tone(55.0 * std::pow(2.0, 2.0 / 12.0), 55.0, 0.06, 1.2, 12.0);
    const auto features = of(sound);
    CHECK(features.pitched);
    CHECK(features.pitchClass == 9);
    CHECK(std::abs(features.cents) < 5.0);
    MESSAGE("pitch " << features.pitchHz << " Hz, glide " << features.glideSemitones);
    // Read over 85 ms, a 60 ms glide reads smaller than its two semitones.
    CHECK(features.glideSemitones > 0.5);
    CHECK(features.glideSemitones < 2.5);
    CHECK(features.lowPeakHz > 45.0);
    CHECK(features.lowPeakHz < 65.0);
    CHECK(features.centroidHz < 200.0);
}

TEST_CASE("Length, attack and tail of a sound whose envelope is known")
{
    // 10 ms rising, then 60 dB a second down: -60 dB one second after the peak.
    auto sound = tone(1000.0, 1000.0, 0.0, 1.5, 0.0);
    for (std::size_t index = 0; index < sound.size(); ++index)
    {
        const auto t = static_cast<double>(index) / rate;
        const auto gain = t < 0.01 ? t / 0.01 : std::pow(10.0, -60.0 * (t - 0.01) / 20.0);
        sound[index] *= static_cast<float>(gain);
    }
    const auto features = of(sound);
    MESSAGE("length " << features.lengthSeconds << " s, attack " << features.attackMs << " ms, tail "
                      << features.tailSeconds << " s");
    CHECK(features.lengthSeconds == doctest::Approx(1.01).epsilon(0.02));
    CHECK(features.attackMs == doctest::Approx(8.0).epsilon(0.15));
    CHECK(features.tailSeconds == doctest::Approx(0.5).epsilon(0.03)); // -10 to -40 dB at 60 dB/s
    CHECK(features.peakDb == doctest::Approx(20.0 * std::log10(0.8)).epsilon(0.05));
}

TEST_CASE("A hat is bright and short, two unrelated channels are wide, a mono one is not")
{
    const auto hat = noise(0.12, 300.0, 1, true);
    const auto features = of(hat);
    CHECK(features.centroidHz > 6000.0);
    CHECK(features.highShare > 0.5);
    CHECK(features.lengthSeconds < 0.25);
    CHECK_FALSE(features.pitched);

    const auto left = noise(0.5, 0.0, 2, false);
    const auto right = noise(0.5, 0.0, 3, false);
    CHECK(measure(left.data(), right.data(), left.size(), rate).width > 0.4);
    CHECK(of(left).width < 0.01);
}

TEST_CASE("A role is what the name says, when the sound does not refuse it")
{
    const auto hat = of(noise(0.12, 300.0, 1, true));
    const auto open = of(noise(0.6, 20.0, 4, true));
    const auto bass = of(tone(55.0, 55.0, 0.0, 1.2, 12.0));
    const auto kick = of(tone(150.0, 50.0, 0.04, 0.35, 60.0));

    CHECK(roleOf("Drums/Hats/HH 01.wav", hat) == Role::closedHat);
    CHECK(roleOf("Drums/Hats/HH 01.wav", open) == Role::openHat); // long: open, whatever the name
    CHECK_FALSE(roleOf("Open Hat 02.wav", hat).has_value());      // « open », and short: refused
    CHECK(roleOf("808 A.wav", bass) == Role::bass808);
    CHECK_FALSE(roleOf("808 A.wav", hat).has_value()); // no pitch, too bright
    CHECK(roleOf("Kick808.wav", kick) == Role::kick);  // a kick of an 808 machine is a kick
    CHECK_FALSE(roleOf("Kick Bright.wav", hat).has_value());
    CHECK_FALSE(roleOf("Ambience 3.wav", kick).has_value());
}

TEST_CASE("A sample's measure is written and read back")
{
    const auto features = of(tone(55.0, 55.0, 0.0, 0.8, 12.0));
    const auto again = Features::fromValue(features.toValue());
    REQUIRE(again.ok());
    CHECK(again.value().pitchClass == features.pitchClass);
    CHECK(again.value().lowPeakHz == doctest::Approx(features.lowPeakHz));
    CHECK(again.value().bandsDb[1] == doctest::Approx(features.bandsDb[1]).epsilon(0.01));
}

namespace
{

// A library built by its numbers: what the choice reads.
Sample sample(std::string path, Role role, double centroid, double tail, double crest)
{
    Sample made;
    made.path = std::move(path);
    made.role = role;
    made.features.centroidHz = centroid;
    made.features.highShare = std::clamp(centroid / 20000.0, 0.0, 1.0);
    made.features.tailSeconds = tail;
    made.features.crestDb = crest;
    made.features.lowProfileDb.fill(-80.0);
    return made;
}

Sample bass(std::string path, int pitchClass, double cents, double hz, std::size_t lowBand)
{
    auto made = sample(std::move(path), Role::bass808, 120.0, 0.8, 12.0);
    made.features.pitched = true;
    made.features.pitchClass = pitchClass;
    made.features.cents = cents;
    made.features.pitchHz = hz;
    made.features.lowProfileDb[lowBand] = -10.0;
    made.features.lowProfileDb[lowBand + 1] = -16.0;
    return made;
}

Sample kickOf(std::string path, std::size_t lowBand, double centroid)
{
    auto made = sample(std::move(path), Role::kick, centroid, 0.2, 14.0);
    made.features.lowProfileDb[lowBand] = -10.0;
    made.features.lowProfileDb[lowBand + 1] = -16.0;
    made.features.lowPeakHz = lowBandCentre(lowBand);
    return made;
}

std::vector<Sample> library()
{
    // Band b of the low profile is centred at 20 Hz · 2^((b + 0.5) / 6): A1,
    // 55 Hz, is band 8; band 14 is 106 Hz, band 15 119 Hz. The axes are
    // z-scores within a role: each role has a sample in the middle of it.
    std::vector<Sample> made{
        bass("808/808 A.wav", 9, 4.0, 55.0, 8),
        bass("808/808 A flat.wav", 9, -30.0, 54.0, 8), // too far from A: refused
        bass("808/808 G.wav", 7, 0.0, 49.0, 7),
        kickOf("Kicks/Kick Same.wav", 8, 330.0), // the nearest the axes, its low end on the 808's: refused
        kickOf("Kicks/Kick Punch.wav", 14, 300.0),
        kickOf("Kicks/Kick Dark.wav", 15, 200.0),
        kickOf("Kicks/Kick Bright.wav", 14, 600.0),
        kickOf("Kicks/Kick Mid.wav", 15, 310.0),
        sample("Snares/Snare 1.wav", Role::snare, 3000.0, 0.2, 12.0),
        sample("Snares/Snare 2.wav", Role::snare, 5000.0, 0.3, 10.0),
        sample("Snares/Snare 3.wav", Role::snare, 4000.0, 0.25, 11.0),
        sample("Hats/HH 1.wav", Role::closedHat, 9000.0, 0.05, 12.0),
        sample("Hats/HH 2.wav", Role::closedHat, 11000.0, 0.06, 11.0),
        sample("Hats/HH 3.wav", Role::closedHat, 10000.0, 0.055, 11.5),
        sample("Hats/OH 1.wav", Role::openHat, 9000.0, 0.4, 12.0),
        sample("Perc/Shaker.wav", Role::percussion, 7000.0, 0.1, 12.0),
    };
    return made;
}

const Pick* pickOf(const Kit& kit, Role role)
{
    const auto found = std::find_if(
        kit.picks.begin(), kit.picks.end(), [role](const Pick& pick) { return pick.role == role; });
    return found != kit.picks.end() ? &*found : nullptr;
}

} // namespace

TEST_CASE("The 808 on the tonic within 15 cents, else on the fifth, else none and the nearest said")
{
    const auto kit = choose(library(), 9, Axes{});
    const auto* bass808 = pickOf(kit, Role::bass808);
    REQUIRE(bass808 != nullptr);
    CHECK(bass808->path == "808/808 A.wav");
    for (const auto& reason : bass808->reasons)
        MESSAGE(reason);

    // In D, the fifth is A.
    const auto inD = choose(library(), 2, Axes{});
    REQUIRE(pickOf(inD, Role::bass808) != nullptr);
    CHECK(pickOf(inD, Role::bass808)->path == "808/808 A.wav");

    // In F#, neither F# nor C#: nothing is made up.
    const auto inFSharp = choose(library(), 6, Axes{});
    CHECK(pickOf(inFSharp, Role::bass808) == nullptr);
    REQUIRE_FALSE(inFSharp.missing.empty());
    MESSAGE(inFSharp.missing.front());
    CHECK(inFSharp.missing.front().find("la plus proche") != std::string::npos);
}

TEST_CASE("The kick leaves the 808 its low end: no correlation of 0.5 or more, a quarter apart at least")
{
    const auto kit = choose(library(), 9, Axes{});
    const auto* kick = pickOf(kit, Role::kick);
    REQUIRE(kick != nullptr);
    CHECK(kick->path != "Kicks/Kick Same.wav");
    const auto lib = library();
    const auto& chosenKick =
        std::find_if(lib.begin(), lib.end(), [&](const Sample& s) { return s.path == kick->path; })->features;
    const auto& bass808 = lib.front().features;
    CHECK(lowCorrelation(chosenKick, bass808) < overlapCorrelation);
    const auto ratio = chosenKick.lowPeakHz / bass808.pitchHz;
    CHECK((ratio >= lowApart || ratio <= 1.0 / lowApart));
}

TEST_CASE("Every element within 0.75 of the others on each axis, and the same library gives the same kit")
{
    auto lib = library();
    const auto kit = choose(lib, 9, Axes{});
    CHECK(kit.picks.size() >= 5);
    for (const auto& a : kit.picks)
        for (const auto& b : kit.picks)
        {
            if (a.outOfColour || b.outOfColour)
                continue; // said out of colour, its gap tested below
            CHECK(std::abs(a.axes.bright - b.axes.bright) <= colourApart);
            CHECK(std::abs(a.axes.ample - b.axes.ample) <= colourApart);
            CHECK(std::abs(a.axes.dirty - b.axes.dirty) <= colourApart);
        }

    // In another order, the same kit.
    std::reverse(lib.begin(), lib.end());
    const auto again = choose(lib, 9, Axes{});
    REQUIRE(again.picks.size() == kit.picks.size());
    for (std::size_t index = 0; index < kit.picks.size(); ++index)
        CHECK(again.picks[index].path == kit.picks[index].path);

    // Brighter wanted: the brighter hat.
    const auto bright = choose(library(), 9, Axes{2.0, 0.0, 0.0});
    REQUIRE(pickOf(bright, Role::closedHat) != nullptr);
    MESSAGE("bright: " << pickOf(bright, Role::closedHat)->path);
}

namespace
{

// A library where no kick is near the 808's colour: one 808, the axes all 0;
// the kicks in two groups whose centroids are a decade apart, at ±1 on the
// brightness axis.
std::vector<Sample> farKicks(std::size_t kicks)
{
    std::vector<Sample> made{bass("808/808 A.wav", 9, 0.0, 55.0, 8)};
    for (std::size_t index = 0; index < kicks; ++index)
        made.push_back(
            kickOf("Kicks/Kick " + std::to_string(index) + ".wav", 14, index % 2 == 0 ? 100.0 : 1000.0));
    made.push_back(sample("Snares/Snare.wav", Role::snare, 3000.0, 0.2, 12.0));
    made.push_back(sample("Hats/HH.wav", Role::closedHat, 9000.0, 0.05, 12.0));
    return made;
}

} // namespace

TEST_CASE(
    "A role of fewer than ten samples, none in colour: its nearest taken, said out of colour with its gap")
{
    const auto kit = choose(farKicks(4), 9, Axes{});
    const auto* kick = pickOf(kit, Role::kick);
    REQUIRE(kick != nullptr);
    CHECK(kick->outOfColour);
    CHECK(kick->colourGap > colourApart);
    const auto said = std::any_of(kick->reasons.begin(),
                                  kick->reasons.end(),
                                  [](const std::string& reason)
                                  { return reason.find("hors couleur") != std::string::npos; });
    CHECK(said);
    for (const auto& reason : kick->reasons)
        MESSAGE(reason);
    // The 808, the kick, the snare, the hat: nothing missing for colour.
    CHECK(kit.picks.size() == 4);
    for (const auto& missing : kit.missing)
        CHECK(missing.find("couleur") == std::string::npos);
    // The others, in colour.
    for (const auto& pick : kit.picks)
        if (pick.role != Role::kick)
            CHECK_FALSE(pick.outOfColour);
}

TEST_CASE("A role of ten samples or more keeps the colour: the element missing, said")
{
    const auto kit = choose(farKicks(smallRole), 9, Axes{});
    CHECK(pickOf(kit, Role::kick) == nullptr);
    REQUIRE_FALSE(kit.missing.empty());
    CHECK(std::any_of(kit.missing.begin(),
                      kit.missing.end(),
                      [](const std::string& missing) {
                          return missing.find("aucun kick assez proche de la couleur du kit") !=
                                 std::string::npos;
                      }));
}
