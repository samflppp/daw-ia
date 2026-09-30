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

} // namespace daw::ui
