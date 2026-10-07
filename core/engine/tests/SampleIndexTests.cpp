#include "daw/engine/SampleIndex.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include <doctest/doctest.h>

using daw::domain::kit::Role;
using daw::engine::SampleIndex;

// The index of the samples (S24): measured once, kept on the machine, read
// back while nothing changed.

namespace
{

constexpr double rate = 48000.0;

void writeWav(const juce::File& file, const std::vector<float>& samples)
{
    static_cast<void>(file.getParentDirectory().createDirectory());
    static_cast<void>(file.deleteFile());
    juce::AudioBuffer<float> buffer{1, static_cast<int>(samples.size())};
    for (std::size_t index = 0; index < samples.size(); ++index)
        buffer.setSample(0, static_cast<int>(index), samples[index]);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(24));
    REQUIRE(writer != nullptr);
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}

std::vector<float> tone(double hz, double seconds, double decayDbPerSecond)
{
    std::vector<float> samples(static_cast<std::size_t>(seconds * rate));
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto t = static_cast<double>(index) / rate;
        samples[index] = static_cast<float>(0.8 * std::pow(10.0, -decayDbPerSecond * t / 20.0) *
                                            std::sin(2.0 * std::numbers::pi * hz * t));
    }
    return samples;
}

std::vector<float> hat(double seconds)
{
    std::vector<float> samples(static_cast<std::size_t>(seconds * rate));
    std::mt19937 random{7};
    std::uniform_real_distribution<float> uniform{-0.5f, 0.5f};
    float previous = 0.0f;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto value = uniform(random);
        samples[index] =
            (value - previous) *
            static_cast<float>(std::pow(10.0, -300.0 * static_cast<double>(index) / rate / 20.0));
        previous = value;
    }
    return samples;
}

struct Library
{
    Library()
    {
        writeWav(folder.getChildFile("808/808 A.wav"), tone(55.0, 1.0, 12.0));
        writeWav(folder.getChildFile("Hats/HH 01.wav"), hat(0.12));
        writeWav(folder.getChildFile("Kicks/Kick 01.wav"), tone(60.0, 0.3, 60.0));
        static_cast<void>(folder.getChildFile("Broken/Snare.wav").create());
        static_cast<void>(folder.getChildFile("Broken/Snare.wav").replaceWithText("not audio"));
    }
    ~Library() { static_cast<void>(folder.deleteRecursively()); }

    std::vector<juce::File> files() const
    {
        return {folder.getChildFile("808/808 A.wav"),
                folder.getChildFile("Hats/HH 01.wav"),
                folder.getChildFile("Kicks/Kick 01.wav"),
                folder.getChildFile("Broken/Snare.wav")};
    }

    juce::File folder{juce::File::createTempFile("kitlibrary")};
};

} // namespace

TEST_CASE("The index measures each sample once, keeps it on the machine, and measures again what changed")
{
    Library library;
    SampleIndex index{library.folder.getChildFile("machine/samples-index.json")};
    std::atomic<bool> cancelled{false};

    const auto first = index.build(library.files(), cancelled, {}, 2);
    CHECK(first.measured == 3);
    CHECK(first.unreadable == 1);
    CHECK(first.reused == 0);
    CHECK(index.store().existsAsFile());
    const auto roles = SampleIndex::library(first);
    REQUIRE(roles.size() == 3);
    CHECK(roles[0].role == Role::bass808);
    CHECK(roles[0].features.pitchClass == 9);
    CHECK(roles[1].role == Role::closedHat);
    CHECK(roles[2].role == Role::kick);

    // Nothing changed: read back, nothing measured.
    const auto second = index.build(library.files(), cancelled, {}, 2);
    CHECK(second.measured == 0);
    CHECK(second.reused == 3);
    REQUIRE(second.entries.size() == 3);
    CHECK(second.entries[0].features.pitchClass == 9);
    CHECK(SampleIndex::library(second).size() == 3);

    // One sample replaced by another sound: it alone is measured again.
    writeWav(library.folder.getChildFile("Kicks/Kick 01.wav"), tone(62.0, 0.4, 50.0));
    static_cast<void>(
        library.folder.getChildFile("Kicks/Kick 01.wav")
            .setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5.0)));
    const auto third = index.build(library.files(), cancelled, {}, 2);
    CHECK(third.measured == 1);
    CHECK(third.reused == 2);
}

TEST_CASE("A cancelled build leaves the index on the machine as it was")
{
    Library library;
    SampleIndex index{library.folder.getChildFile("machine/samples-index.json")};
    std::atomic<bool> cancelled{true};
    std::size_t calls = 0;
    const auto built = index.build(library.files(), cancelled, [&](std::size_t, std::size_t) { ++calls; }, 2);
    CHECK(built.cancelled);
    CHECK(built.measured == 0);
    CHECK(calls == 0);
    CHECK_FALSE(index.store().existsAsFile());
}
