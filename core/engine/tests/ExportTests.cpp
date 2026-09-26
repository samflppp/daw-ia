#include "daw/engine/Export.h"

#include <cmath>
#include <string>

#include <doctest/doctest.h>

using daw::engine::ExportFormat;
using daw::engine::ExportSettings;

namespace
{

constexpr double seconds = 2.0;
constexpr double frequency = 440.0;

// A sine at the given peak, the same on both channels.
juce::AudioBuffer<float> sine(double sampleRate, float peak)
{
    const auto frames = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> audio{2, frames};
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto value = peak * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * frequency *
                                                              static_cast<double>(frame) / sampleRate));
        audio.setSample(0, frame, value);
        audio.setSample(1, frame, value);
    }
    return audio;
}

double rmsDb(const juce::AudioBuffer<float>& audio)
{
    return 20.0 * std::log10(static_cast<double>(audio.getRMSLevel(0, 0, audio.getNumSamples())));
}

struct ReadBack
{
    juce::AudioBuffer<float> audio;
    double sampleRate{0.0};
};

ReadBack
exportAndRead(const juce::AudioBuffer<float>& audio, double sampleRate, const ExportSettings& settings)
{
    const juce::TemporaryFile file{daw::engine::extensionFor(settings.format)};
    const auto error = daw::engine::writeExport(audio, sampleRate, file.getFile(), settings);
    INFO(error.toStdString());
    REQUIRE(error.isEmpty());
    REQUIRE(file.getFile().getSize() > 0);

    ReadBack back;
    REQUIRE(daw::engine::readExport(file.getFile(), back.audio, back.sampleRate));
    return back;
}

} // namespace

TEST_CASE("export: WAV and FLAC give back what was written")
{
    const auto rate = 48000.0;
    const auto audio = sine(rate, 0.5f);

    for (const auto& settings : {ExportSettings{ExportFormat::wav, 16, 0},
                                 ExportSettings{ExportFormat::wav, 24, 0},
                                 ExportSettings{ExportFormat::flac, 16, 0},
                                 ExportSettings{ExportFormat::flac, 24, 0}})
    {
        CAPTURE(daw::engine::extensionFor(settings.format).toStdString());
        CAPTURE(settings.bitDepth);
        const auto back = exportAndRead(audio, rate, settings);
        CHECK(back.sampleRate == rate);
        CHECK(back.audio.getNumChannels() == 2);
        CHECK(back.audio.getNumSamples() == audio.getNumSamples());
        CHECK(std::abs(rmsDb(back.audio) - rmsDb(audio)) < 0.01);
    }
}

TEST_CASE("export: 32-bit WAV keeps a level past full scale")
{
    const auto rate = 44100.0;
    const auto audio = sine(rate, 2.0f);
    const auto back = exportAndRead(audio, rate, ExportSettings{ExportFormat::wav, 32, 0});
    CHECK(back.audio.getMagnitude(0, 0, back.audio.getNumSamples()) > 1.9f);
}

// Media Foundation is Windows's: elsewhere the export says so and writes nothing.
#if JUCE_WINDOWS
TEST_CASE("export: MP3 and AAC through Media Foundation")
{
    for (const auto rate : {44100.0, 48000.0, 96000.0})
    {
        const auto audio = sine(rate, 0.5f);
        for (const auto& settings :
             {ExportSettings{ExportFormat::mp3, 0, 192}, ExportSettings{ExportFormat::aac, 0, 160}})
        {
            CAPTURE(rate);
            CAPTURE(daw::engine::extensionFor(settings.format).toStdString());
            const auto back = exportAndRead(audio, rate, settings);

            // 96 kHz is resampled to 48 kHz: the encoders take no more.
            CHECK(back.sampleRate == (rate == 96000.0 ? 48000.0 : rate));
            CHECK(back.audio.getNumChannels() == 2);

            // The encoders pad the start and the end by a frame or two.
            const auto length = back.audio.getNumSamples() / back.sampleRate;
            CHECK(length > seconds - 0.01);
            CHECK(length < seconds + 0.15);

            // A sine survives a lossy codec within a fraction of a decibel.
            CHECK(std::abs(rmsDb(back.audio) - rmsDb(audio)) < 0.5);
        }
    }
}

#endif

TEST_CASE("export: nothing rendered, nothing written")
{
    const juce::TemporaryFile file{".wav"};
    juce::AudioBuffer<float> empty;
    CHECK(daw::engine::writeExport(empty, 48000.0, file.getFile(), ExportSettings{}).isNotEmpty());
}
