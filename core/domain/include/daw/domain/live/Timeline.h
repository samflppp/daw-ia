#pragma once

namespace daw::domain::live
{

// Where in an audio block a note played live is placed (S23, decided on 6
// October 2026: « régulière »). An event played at t, on the input clock, is
// placed at t plus one block and a margin: every note waits the same, so a
// run of notes keeps the spacing it was played with, instead of each landing
// at the start of whichever block follows it — up to a block of jitter, 10 ms
// on the default driver. The cost is that wait, said on the screen.
//
// The margin is the callback's own jitter: a note that arrives just before a
// late callback would otherwise fall before the start of the block and be
// pulled forward to it. With the margin no note is ever pulled: one that
// falls past the end of a block waits for the next, where it lands exactly.
//
// The audio thread calls begin() at the start of each block with the input
// clock. A callback comes a little early or late; the timeline follows the
// clock slowly, so that jitter does not move the notes, and starts again
// from the clock when it is far off (the first block, a device that
// stopped, a block lost).
//
// The audio thread only; nothing allocates, locks or waits.
class Timeline
{
public:
    // Where this block starts on the input clock.
    double begin(double clock, int samples, double sampleRate) noexcept;

    // The sample of the current block an event played at `seconds` falls
    // on: at least 0 (late, it plays at once), at or past the block's length
    // when it belongs to a later block.
    [[nodiscard]] int offsetOf(double seconds) const noexcept;

    [[nodiscard]] double start() const noexcept { return start_; }
    [[nodiscard]] double delay() const noexcept { return delay_; }

    // How far the clock may be from where the timeline expects it before it
    // starts again from the clock: two blocks.
    static constexpr double restartBlocks = 2.0;

    // The share of the distance to the clock caught up at each block.
    static constexpr double follow = 0.02;

    // The margin over one block: a quarter of it, a millisecond at least.
    static constexpr double marginShare = 0.25;
    static constexpr double marginSeconds = 0.001;

private:
    bool started_{false};
    double start_{0.0};
    double expected_{0.0};
    double delay_{0.0};
    double sampleRate_{44100.0};
};

} // namespace daw::domain::live
