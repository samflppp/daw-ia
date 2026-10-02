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

// The playhead as drawn, in seconds of the song, between the positions the
// engine gives once per audio block (S19).
//
// Carrying the last position forward by the time since it was seen, and never
// backwards (S18 bis), left an image without movement each time a block came
// in behind that guess: one image in twenty, a jolt the eye catches. Here the
// playhead moves on by the time that passed since the last image, and is
// pulled towards the engine's position a little at each image, over
// `pullMs`. It never goes back, but for the engine going back by more than a
// block (a loop, a click on the ruler), and it never runs more than a block
// ahead of the engine: a device that stalls is waited for.
//
// Before the engine has moved once, the playhead waits where it is: the
// device takes a few hundred milliseconds to start, and the playhead has
// nowhere to go until it does.
struct DrawnPlayhead
{
    static constexpr double blockSeconds = 0.05;
    static constexpr double pullMs = 100.0;

    // Where to draw the playhead, given the engine's position and the time.
    // Called as often as anyone draws; the result depends on the time, not on
    // how many calls there were.
    double advance(double engineSeconds, double nowMs) noexcept;

    // Stopped: the next call starts from the engine's position.
    void stop() noexcept;

    bool started{false};
    bool moving{false};
    double seen{0.0};
    double seenAtMs{0.0};
    double lastMs{0.0};
    double drawn{0.0};
};

} // namespace daw::ui
