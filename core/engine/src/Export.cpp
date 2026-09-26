#include "daw/engine/Export.h"

#include "MediaFoundation.h"
#include "daw/engine/Rendering.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace daw::engine
{
namespace
{

// The rates the Media Foundation encoders take. Anything else is resampled to
// the second one before encoding.
constexpr double cdRate = 44100.0;
constexpr double encoderRate = 48000.0;

// The quality FLAC compresses at: JUCE's default, the middle of its range.
constexpr int flacQuality = 5;

juce::AudioBuffer<float> resampled(const juce::AudioBuffer<float>& audio, double from, double to)
{
    const auto ratio = from / to;
    const auto frames = static_cast<int>(std::floor(audio.getNumSamples() / ratio));
    juce::AudioBuffer<float> out{audio.getNumChannels(), frames};

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        juce::WindowedSincInterpolator interpolator;
        interpolator.process(ratio, audio.getReadPointer(channel), out.getWritePointer(channel), frames);
    }
    return out;
}

juce::String writeWithJuce(juce::AudioFormat& format,
                           const juce::AudioBuffer<float>& audio,
                           double sampleRate,
                           const juce::File& target,
                           int bitDepth,
                           bool floating,
                           int quality)
{
    static_cast<void>(target.deleteFile());
    std::unique_ptr<juce::OutputStream> stream = target.createOutputStream();
    if (stream == nullptr)
        return juce::String{u8"impossible de créer le fichier "} + target.getFileName();

    const auto options =
        juce::AudioFormatWriterOptions{}
            .withSampleRate(sampleRate)
            .withNumChannels(audio.getNumChannels())
            .withBitsPerSample(bitDepth)
            .withQualityOptionIndex(quality)
            .withSampleFormat(floating ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                       : juce::AudioFormatWriterOptions::SampleFormat::integral);
    auto writer = format.createWriterFor(stream, options);
    if (writer == nullptr)
        return format.getFormatName() + juce::String{u8" refuse ces réglages"};

    if (!writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        return juce::String{u8"écriture interrompue : "} + target.getFileName();
    return {};
}

} // namespace

juce::String extensionFor(ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::wav:
        return ".wav";
    case ExportFormat::flac:
        return ".flac";
    case ExportFormat::mp3:
        return ".mp3";
    case ExportFormat::aac:
        return ".m4a";
    }
    return ".wav";
}

std::vector<int> bitRatesFor(ExportFormat format)
{
    switch (format)
    {
    case ExportFormat::mp3:
        return {128, 192, 256, 320};
    case ExportFormat::aac:
        return {96, 128, 160, 192};
    case ExportFormat::wav:
    case ExportFormat::flac:
        break;
    }
    return {};
}

juce::String writeExport(const juce::AudioBuffer<float>& audio,
                         double sampleRate,
                         const juce::File& target,
                         const ExportSettings& settings)
{
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0)
        return u8"rien à exporter : le rendu est vide";

    switch (settings.format)
    {
    case ExportFormat::wav:
    {
        juce::WavAudioFormat wav;
        const auto bits = settings.bitDepth == 16 || settings.bitDepth == 24 ? settings.bitDepth : 32;
        return writeWithJuce(wav, audio, sampleRate, target, bits, bits == 32, 0);
    }
    case ExportFormat::flac:
    {
        juce::FlacAudioFormat flac;
        return writeWithJuce(
            flac, audio, sampleRate, target, settings.bitDepth == 16 ? 16 : 24, false, flacQuality);
    }
    case ExportFormat::mp3:
    case ExportFormat::aac:
    {
        const auto takesRate = sampleRate == cdRate || sampleRate == encoderRate;
        const auto rate = takesRate ? sampleRate : encoderRate;
        const auto source = takesRate ? audio : resampled(audio, sampleRate, encoderRate);
        return mediaFoundation::encode(
            source, rate, target, settings.format == ExportFormat::aac, settings.kilobitsPerSecond);
    }
    }
    return u8"format inconnu";
}

juce::String exportSong(tracktion::Edit& edit, const juce::File& target, const ExportSettings& settings)
{
    if (edit.getLength() <= tracktion::TimeDuration{})
        return u8"rien à exporter : la playlist est vide";

    // The render is renderAsPlayed's, the one the verification measures: a
    // muted track stays muted, the master chain is heard.
    const juce::TemporaryFile rendered{".wav"};
    if (!renderAsPlayed(edit, rendered.getFile()))
        return u8"le rendu a échoué";

    juce::AudioBuffer<float> audio;
    double sampleRate = 0.0;
    if (!readExport(rendered.getFile(), audio, sampleRate))
        return u8"le rendu est illisible";

    return writeExport(audio, sampleRate, target, settings);
}

bool readExport(const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate)
{
    const auto extension = file.getFileExtension().toLowerCase();
    if (extension == ".mp3" || extension == ".m4a")
        return mediaFoundation::decode(file, audio, sampleRate);

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr)
        return false;

    audio.setSize(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    if (!reader->read(&audio, 0, static_cast<int>(reader->lengthInSamples), 0, true, true))
        return false;
    sampleRate = reader->sampleRate;
    return true;
}

} // namespace daw::engine
