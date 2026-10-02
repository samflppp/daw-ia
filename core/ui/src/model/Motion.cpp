#include "daw/ui/model/Motion.h"

#include <algorithm>

namespace daw::ui
{

double Glide::at(double nowMs) const noexcept
{
    if (durationMs <= 0.0 || nowMs >= startMs + durationMs)
        return to;
    if (nowMs <= startMs)
        return from;

    const auto t = (nowMs - startMs) / durationMs;
    const auto left = 1.0 - t;
    return from + (to - from) * (1.0 - left * left * left);
}

bool Glide::done(double nowMs) const noexcept
{
    return durationMs <= 0.0 || nowMs >= startMs + durationMs;
}

float FallingLevel::advance(
    float readDb, double nowMs, float dbPerSecond, double holdMs, float floorDb) noexcept
{
    const auto elapsed = std::max(0.0, nowMs - lastMs);
    lastMs = nowMs;

    if (readDb >= db)
    {
        db = readDb;
        risenAtMs = nowMs;
        return db;
    }

    // Held where it rose, then falling: the part of the elapsed time spent
    // past the hold is what it falls by.
    const auto fallingFor = std::min(elapsed, std::max(0.0, nowMs - (risenAtMs + holdMs)));
    const auto fallen = static_cast<float>(fallingFor / 1000.0) * dbPerSecond;
    db = std::max({readDb, db - fallen, floorDb});
    return db;
}

double DrawnPlayhead::advance(double engineSeconds, double nowMs) noexcept
{
    if (!started)
    {
        started = true;
        moving = false;
        seen = drawn = engineSeconds;
        seenAtMs = lastMs = nowMs;
        return drawn;
    }

    const auto elapsed = std::clamp((nowMs - lastMs) / 1000.0, 0.0, blockSeconds);
    lastMs = nowMs;

    if (engineSeconds != seen)
    {
        const auto wentBack = engineSeconds < seen;
        seen = engineSeconds;
        seenAtMs = nowMs;

        // The first block that moves: from here on, the playhead runs.
        if (!moving)
        {
            moving = true;
            drawn = std::max(drawn, engineSeconds);
            return drawn;
        }

        if (wentBack && drawn - engineSeconds > blockSeconds)
        {
            drawn = engineSeconds;
            return drawn;
        }
    }

    if (!moving)
        return drawn;

    const auto estimate = seen + std::min((nowMs - seenAtMs) / 1000.0, blockSeconds);
    const auto predicted = drawn + elapsed;
    const auto error = estimate - predicted;

    // The engine ahead by more than a block is a jump forward: follow it.
    if (error > blockSeconds)
    {
        drawn = estimate;
        return drawn;
    }

    const auto pulled = predicted + error * std::min(1.0, elapsed * 1000.0 / pullMs);
    drawn = std::max(drawn, std::min(pulled, seen + blockSeconds));
    return drawn;
}

void DrawnPlayhead::stop() noexcept
{
    started = false;
    moving = false;
}

} // namespace daw::ui
