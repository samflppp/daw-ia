#pragma once

namespace daw::ui
{

// The arithmetic of what moves on screen in the fluid mode (S18 bis): a value
// that glides to another, a level that falls. Pure C++, no JUCE, no clock of
// its own: every call is handed the time, so a test decides what time it is.
//
// The durations and rates come from the tokens (motion.*); none is written
// here.

// A value on its way from one place to another: fast at the start, slowing
// down as it arrives (a cubic ease-out). Used for the page that turns under
// the playhead and for the zoom.
struct Glide
{
    double from{0.0};
    double to{0.0};
    double startMs{0.0};
    double durationMs{0.0};

    // Where the value is at `nowMs`: `from` at the start, `to` from the end
    // on, and `to` at once for a glide of no duration.
    [[nodiscard]] double at(double nowMs) const noexcept;
    [[nodiscard]] bool done(double nowMs) const noexcept;
};

// A level as a meter shows it: it rises at once to what was read, and falls
// at a fixed rate, after holding for a while when it is a peak. The meter
// shows the fall between two readings, which arrive less often than images.
struct FallingLevel
{
    float db{-100.0f};
    double lastMs{0.0};
    double risenAtMs{0.0};

    // Moves the level towards `readDb` and returns it. `holdMs` is how long
    // a level stays where it rose before it starts to fall; never below
    // `floorDb`.
    float advance(float readDb, double nowMs, float dbPerSecond, double holdMs, float floorDb) noexcept;
};

} // namespace daw::ui
