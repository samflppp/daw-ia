#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace daw::ui
{

// The shape of a sample, measured once from its bytes and kept.
//
// A fixed number of buckets per second, each holding the lowest and the
// highest sample it covers, channels folded together. Drawing a clip asks for
// columns(): as many min/max pairs as it has pixels, gathered from the
// buckets. That gathering is cheap and it is all a repaint does; reading and
// decoding the audio is done once per sample, on another thread, and the
// result is indexed by the sample's digest — the same sample dropped ten
// times is measured once.
//
// Pure C++, no JUCE: the arithmetic is what a test checks.
struct WaveformPeaks
{
    static constexpr int bucketsPerSecond = 400;

    double seconds{0.0};
    std::vector<float> minimum;
    std::vector<float> maximum;

    // Measures `length` samples of `channelCount` channels.
    [[nodiscard]] static WaveformPeaks
    measure(const float* const* channels, int channelCount, std::int64_t length, double sampleRate);

    struct Column
    {
        float minimum{0.0f};
        float maximum{0.0f};
    };

    // `count` columns covering [fromSeconds, toSeconds) of the sample. A column
    // past the end of the sample is silent.
    [[nodiscard]] std::vector<Column> columns(double fromSeconds, double toSeconds, int count) const;
};

} // namespace daw::ui
