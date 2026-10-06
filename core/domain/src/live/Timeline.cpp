#include "daw/domain/live/Timeline.h"

#include <algorithm>
#include <cmath>

namespace daw::domain::live
{

double Timeline::begin(double clock, int samples, double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto length = static_cast<double>(std::max(samples, 1)) / sampleRate_;

    if (!started_ || std::abs(clock - expected_) > restartBlocks * length)
        start_ = clock;
    else
        start_ = expected_ + (clock - expected_) * follow;

    started_ = true;
    expected_ = start_ + length;
    delay_ = length + std::max(marginSeconds, marginShare * length);
    return start_;
}

int Timeline::offsetOf(double seconds) const noexcept
{
    const auto at = std::floor((seconds + delay_ - start_) * sampleRate_);
    if (at <= 0.0)
        return 0;
    return at >= 1.0e9 ? 1'000'000'000 : static_cast<int>(at);
}

} // namespace daw::domain::live
