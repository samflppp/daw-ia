#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace daw::engine
{

// Exporting the song: the arrangement rendered the way it plays, then written
// in the format the user chose.
//
//   WAV    16 or 24 bits, or 32-bit float that keeps a level past full scale
//   FLAC   16 or 24 bits, lossless
//   MP3    128 to 320 kbit/s
//   AAC    96 to 192 kbit/s, in an .m4a
//
// WAV and FLAC are JUCE's own writers. MP3 and AAC are Windows's: the Media
// Foundation encoders every Windows 10 ships, so nothing is bundled and no
// codec licence is ours to carry. The price is that they exist on Windows
// only, and not on the "N" editions without the Media Feature Pack; the export
// says so rather than writing nothing.
enum class ExportFormat
{
    wav,
    flac,
    mp3,
    aac
};

struct ExportSettings
{
    ExportFormat format{ExportFormat::wav};

    // WAV: 16, 24 or 32 (float). FLAC: 16 or 24. Ignored for MP3 and AAC.
    int bitDepth{24};

    // MP3 and AAC. The encoders take a fixed list; the nearest one is used.
    int kilobitsPerSecond{192};
};

// ".wav", ".flac", ".mp3", ".m4a".
[[nodiscard]] juce::String extensionFor(ExportFormat format);

// The bit rates the encoder of that format offers, in kbit/s. Empty for WAV
// and FLAC.
[[nodiscard]] std::vector<int> bitRatesFor(ExportFormat format);

// Writes audio already rendered into the chosen format. The error, in French,
// or an empty string.
[[nodiscard]] juce::String writeExport(const juce::AudioBuffer<float>& audio,
                                       double sampleRate,
                                       const juce::File& target,
                                       const ExportSettings& settings);

// Renders the song (renderAsPlayed, to a temporary WAV) and writes it. Run on
// the message thread, which it holds for the length of the render.
[[nodiscard]] juce::String
exportSong(tracktion::Edit& edit, const juce::File& target, const ExportSettings& settings);

// Reads an exported file back, whatever its format. The verification and the
// tests measure what was written with it.
[[nodiscard]] bool readExport(const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate);

} // namespace daw::engine
