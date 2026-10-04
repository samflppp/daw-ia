#include "daw/domain/direction/Reading.h"
#include "daw/domain/serialization/Json.h"

#include <cmath>
#include <functional>
#include <map>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;

namespace
{

constexpr double rate = 44100.0;

double frequencyOf(int midi)
{
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

// A reference made of known parts: its tempo, its chords, and which stems
// play in which bar are all chosen here.
struct Song
{
    double bpm{120.0};
    int bars{24};
    std::vector<std::vector<int>> chords; // per bar, cycling: MIDI notes of the triad
    std::vector<int> melody;              // per beat, cycling: MIDI notes
    // per bar: does each stem play (vocals, drums, bass, other)
    std::function<std::array<bool, 4>(int bar)> plays;
};

direction::Stereo silence(std::size_t frames)
{
    return {std::vector<float>(frames, 0.0f), std::vector<float>(frames, 0.0f)};
}

void add(direction::Stereo& to, std::size_t at, double value)
{
    if (at >= to.left.size())
        return;
    to.left[at] += static_cast<float>(value);
    to.right[at] += static_cast<float>(value);
}

std::map<std::string, direction::Stereo> render(const Song& song)
{
    const auto beatFrames = static_cast<std::size_t>(rate * 60.0 / song.bpm);
    const auto barFrames = 4 * beatFrames;
    const auto frames = barFrames * static_cast<std::size_t>(song.bars);
    std::map<std::string, direction::Stereo> stems{{"vocals", silence(frames)},
                                                   {"drums", silence(frames)},
                                                   {"bass", silence(frames)},
                                                   {"other", silence(frames)}};
    std::mt19937 random{7};
    std::normal_distribution<double> noise{0.0, 1.0};

    for (int bar = 0; bar < song.bars; ++bar)
    {
        const auto playing = song.plays(bar);
        const auto& chord = song.chords[static_cast<std::size_t>(bar) % song.chords.size()];
        const auto barStart = static_cast<std::size_t>(bar) * barFrames;
        for (int beat = 0; beat < 4; ++beat)
        {
            const auto start = barStart + static_cast<std::size_t>(beat) * beatFrames;
            for (std::size_t i = 0; i < beatFrames; ++i)
            {
                const auto t = static_cast<double>(i) / rate;
                if (playing[1])
                {
                    // A kick on 1 and 3, a snare on 2 and 4, hats on the eighths.
                    if (t < 0.25)
                        add(stems["drums"],
                            start + i,
                            beat % 2 == 0
                                ? 0.8 * std::sin(2.0 * std::numbers::pi * 55.0 * t) * std::exp(-t / 0.08)
                                : 0.4 * noise(random) * std::exp(-t / 0.05));
                    const auto half = static_cast<double>(beatFrames) / 2.0 / rate;
                    const auto sinceHat = std::fmod(t, half);
                    if (sinceHat < 0.03)
                        add(stems["drums"], start + i, 0.15 * noise(random) * std::exp(-sinceHat / 0.008));
                }
                if (playing[2])
                    add(stems["bass"],
                        start + i,
                        0.3 * std::sin(2.0 * std::numbers::pi * frequencyOf(chord[0] - 24) * t) *
                            std::exp(-t / 0.4));
                if (playing[3])
                {
                    double tone = 0.0;
                    for (const auto note : chord)
                        tone += std::sin(2.0 * std::numbers::pi * frequencyOf(note) * t) +
                                0.3 * std::sin(2.0 * std::numbers::pi * frequencyOf(note + 12) * t);
                    add(stems["other"], start + i, 0.06 * tone * std::exp(-t / 0.6));
                }
                if (playing[0])
                {
                    const auto note =
                        song.melody[static_cast<std::size_t>(bar * 4 + beat) % song.melody.size()];
                    add(stems["vocals"],
                        start + i,
                        0.15 * std::sin(2.0 * std::numbers::pi * frequencyOf(note) * t) *
                            std::min(1.0, t / 0.02));
                }
            }
        }
    }
    return stems;
}

// 120 BPM in A minor: Am Dm E Am, the G sharp of E saying minor; eight bars of
// drums, eight with the bass, eight with everything.
Song referenceA()
{
    Song song;
    song.bpm = 120.0;
    song.bars = 24;
    song.chords = {{57, 60, 64}, {50, 53, 57}, {52, 56, 59}, {57, 60, 64}};
    song.melody = {69, 72, 76, 72, 69, 68, 71, 69, 69, 71, 72, 76, 74, 72, 71, 69};
    song.plays = [](int bar)
    {
        if (bar < 8)
            return std::array<bool, 4>{false, true, false, false};
        if (bar < 16)
            return std::array<bool, 4>{false, true, true, false};
        return std::array<bool, 4>{true, true, true, true};
    };
    return song;
}

// 92 BPM in E major: E A B E; eight bars of everything, eight of drums and bass.
Song referenceB()
{
    Song song;
    song.bpm = 92.0;
    song.bars = 16;
    song.chords = {{64, 68, 71}, {57, 61, 64}, {59, 63, 66}, {64, 68, 71}};
    song.melody = {76, 75, 73, 71, 68, 71, 73, 76, 76, 78, 80, 78, 76, 73, 71, 76};
    song.plays = [](int bar)
    {
        if (bar < 8)
            return std::array<bool, 4>{true, true, true, true};
        return std::array<bool, 4>{false, true, true, false};
    };
    return song;
}

} // namespace

TEST_CASE("A reference's tempo is read within a beat per minute")
{
    for (const auto& song : {referenceA(), referenceB()})
    {
        const auto reading = direction::read(render(song), rate, "ref.wav", std::string(64, 'a'));
        REQUIRE(reading.bpm.has_value());
        MESSAGE("tempo lu " << *reading.bpm << " pour " << song.bpm << ", confiance "
                            << reading.bpmConfidence);
        CHECK(std::abs(*reading.bpm - song.bpm) <= 1.0);
    }
}

TEST_CASE("A reference's key and mode are read, and trusted")
{
    const auto a = direction::read(render(referenceA()), rate, "a.wav", std::string(64, 'a'));
    MESSAGE("A : confiance " << a.keyConfidence);
    REQUIRE(a.key.has_value());
    CHECK(*a.key == generation::Key{9, generation::Mode::minor});

    const auto b = direction::read(render(referenceB()), rate, "b.wav", std::string(64, 'b'));
    MESSAGE("B : confiance " << b.keyConfidence);
    REQUIRE(b.key.has_value());
    CHECK(*b.key == generation::Key{4, generation::Mode::major});
}

TEST_CASE("Am F C G and E B C#m A: relatives the notes cannot tell apart, the key is offered, not guessed")
{
    struct Case
    {
        Song song;
        generation::Key major;
        generation::Key minor;
    };
    auto aeolian = referenceA();
    aeolian.chords = {{57, 60, 64}, {53, 57, 60}, {60, 64, 67}, {55, 59, 62}};
    aeolian.melody = {69, 72, 76, 72, 69, 67, 65, 64, 69, 71, 72, 76, 74, 72, 71, 69};
    auto pop = referenceB();
    pop.chords = {{64, 68, 71}, {59, 63, 66}, {61, 64, 68}, {57, 61, 64}};

    for (const auto& item : {Case{aeolian, {0, generation::Mode::major}, {9, generation::Mode::minor}},
                             Case{pop, {4, generation::Mode::major}, {1, generation::Mode::minor}}})
    {
        const auto reading = direction::read(render(item.song), rate, "ambigu.wav", std::string(64, 'd'));
        MESSAGE("confiance " << reading.keyConfidence);
        CHECK_FALSE(reading.key.has_value());
        REQUIRE(reading.keyCandidates.size() == 2);
        const auto relative = [&item](const generation::Key& k)
        { return k == item.major || k == item.minor; };
        CHECK(relative(reading.keyCandidates[0]));
        CHECK(relative(reading.keyCandidates[1]));
    }
}

TEST_CASE("A reference's sections are found where what plays changes, within a bar")
{
    const auto a = direction::read(render(referenceA()), rate, "a.wav", std::string(64, 'a'));
    const auto barA = 4.0 * 60.0 / 120.0;
    REQUIRE(a.sections.size() == 3);
    CHECK(std::abs(a.sections[1].fromSeconds - 8 * barA) <= barA);
    CHECK(std::abs(a.sections[2].fromSeconds - 16 * barA) <= barA);
    CHECK(a.sections[0].label != a.sections[1].label);
    CHECK(a.sections[1].label != a.sections[2].label);
    // Who plays: the intro is drums alone, the last section everyone.
    CHECK(a.sections[0].activity[1] > 0.9);
    CHECK(a.sections[0].activity[2] < 0.1);
    CHECK(a.sections[2].activity[0] > 0.9);

    const auto b = direction::read(render(referenceB()), rate, "b.wav", std::string(64, 'b'));
    const auto barB = 4.0 * 60.0 / 92.0;
    REQUIRE(b.sections.size() == 2);
    CHECK(std::abs(b.sections[1].fromSeconds - 8 * barB) <= barB);
}

TEST_CASE("A reference's stems stand against the whole as they were mixed")
{
    const auto a = direction::read(render(referenceA()), rate, "a.wav", std::string(64, 'a'));
    REQUIRE(a.stems.size() == 4);
    // The vocals play a third of the song, the drums all of it.
    CHECK(a.stems.at("drums").activeShare > 0.9);
    CHECK(a.stems.at("vocals").activeShare < 0.5);
    for (const auto& [name, stem] : a.stems)
        CHECK(stem.balanceDb < 0.0);
}

TEST_CASE("Noise has no key and no tempo: nothing is guessed")
{
    std::mt19937 random{3};
    std::normal_distribution<double> noise{0.0, 0.1};
    direction::Stereo hiss{std::vector<float>(static_cast<std::size_t>(20 * rate)),
                           std::vector<float>(static_cast<std::size_t>(20 * rate))};
    for (std::size_t i = 0; i < hiss.left.size(); ++i)
        hiss.left[i] = hiss.right[i] = static_cast<float>(noise(random));
    const auto reading = direction::read({{"other", hiss}}, rate, "bruit.wav", std::string(64, 'c'));
    MESSAGE("confiances : tempo " << reading.bpmConfidence << ", tonalité " << reading.keyConfidence);
    CHECK_FALSE(reading.key.has_value());
    CHECK_FALSE(reading.bpm.has_value());
}

TEST_CASE("A reading goes to JSON and back unchanged")
{
    const auto a = direction::read(render(referenceA()), rate, "a.wav", std::string(64, 'a'));
    const auto text = json::write(a.toValue());
    const auto back = direction::Reading::fromValue(json::read(text).value());
    REQUIRE(back.ok());
    CHECK(json::write(back.value().toValue()) == text);
}
