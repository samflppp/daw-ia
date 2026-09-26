#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw::engine::mediaFoundation
{

// Windows's own encoders and decoders, behind two calls. Only Export.cpp uses
// them; everything else in the engine speaks JUCE.
//
// Both take 44.1 or 48 kHz; the caller resamples anything else before.

// An MP3 (aac false) or an AAC in an MPEG-4 file (aac true), at the bit rate
// the encoder offers nearest to the one asked for. The error, or empty.
[[nodiscard]] juce::String encode(const juce::AudioBuffer<float>& audio,
                                  double sampleRate,
                                  const juce::File& target,
                                  bool aac,
                                  int kilobitsPerSecond);

// Any file Media Foundation can read, decoded to float.
[[nodiscard]] bool decode(const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate);

} // namespace daw::engine::mediaFoundation
