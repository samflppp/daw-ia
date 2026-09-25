#include "daw/ui/model/WaveformPeaks.h"

#include <algorithm>
#include <cmath>

namespace daw::ui
{

WaveformPeaks
WaveformPeaks::measure(const float* const* channels, int channelCount, std::int64_t length, double sampleRate)
{
    WaveformPeaks peaks;
    if (channels == nullptr || channelCount <= 0 || length <= 0 || sampleRate <= 0.0)
        return peaks;

    peaks.seconds = static_cast<double>(length) / sampleRate;
    const auto buckets = std::max<std::int64_t>(
        1, static_cast<std::int64_t>(std::ceil(peaks.seconds * static_cast<double>(bucketsPerSecond))));
    peaks.minimum.assign(static_cast<std::size_t>(buckets), 0.0f);
    peaks.maximum.assign(static_cast<std::size_t>(buckets), 0.0f);

    const auto perBucket = static_cast<double>(length) / static_cast<double>(buckets);
    for (std::int64_t bucket = 0; bucket < buckets; ++bucket)
    {
        const auto from = static_cast<std::int64_t>(std::floor(static_cast<double>(bucket) * perBucket));
        const auto to = std::min(
            length, static_cast<std::int64_t>(std::floor(static_cast<double>(bucket + 1) * perBucket)));

        float low = 0.0f;
        float high = 0.0f;
        bool first = true;
        for (int channel = 0; channel < channelCount; ++channel)
        {
            const auto* data = channels[channel];
            if (data == nullptr)
                continue;
            for (auto index = from; index < std::max(to, from + 1) && index < length; ++index)
            {
                const auto sample = data[index];
                low = first ? sample : std::min(low, sample);
                high = first ? sample : std::max(high, sample);
                first = false;
            }
        }

        peaks.minimum[static_cast<std::size_t>(bucket)] = low;
        peaks.maximum[static_cast<std::size_t>(bucket)] = high;
    }

    return peaks;
}

std::vector<WaveformPeaks::Column>
WaveformPeaks::columns(double fromSeconds, double toSeconds, int count) const
{
    std::vector<Column> result(static_cast<std::size_t>(std::max(0, count)));
    if (count <= 0 || minimum.empty() || toSeconds <= fromSeconds)
        return result;

    const auto buckets = static_cast<double>(minimum.size());
    const auto secondsPerColumn = (toSeconds - fromSeconds) / static_cast<double>(count);

    for (int column = 0; column < count; ++column)
    {
        const auto start = fromSeconds + secondsPerColumn * static_cast<double>(column);
        const auto end = start + secondsPerColumn;
        if (start >= seconds || end <= 0.0)
            continue;

        auto first = static_cast<std::int64_t>(std::floor(start * static_cast<double>(bucketsPerSecond)));
        auto last = static_cast<std::int64_t>(std::ceil(end * static_cast<double>(bucketsPerSecond)));
        first = std::clamp<std::int64_t>(first, 0, static_cast<std::int64_t>(buckets) - 1);
        last = std::clamp<std::int64_t>(last, first + 1, static_cast<std::int64_t>(buckets));

        auto& out = result[static_cast<std::size_t>(column)];
        out.minimum = minimum[static_cast<std::size_t>(first)];
        out.maximum = maximum[static_cast<std::size_t>(first)];
        for (auto bucket = first + 1; bucket < last; ++bucket)
        {
            out.minimum = std::min(out.minimum, minimum[static_cast<std::size_t>(bucket)]);
            out.maximum = std::max(out.maximum, maximum[static_cast<std::size_t>(bucket)]);
        }
    }

    return result;
}

} // namespace daw::ui
