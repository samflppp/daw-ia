#include "EditClock.h"

#include <algorithm>

namespace daw::app
{

EditClock::EditClock(tracktion::Edit& edit) noexcept
    : edit_(edit)
{
}

double EditClock::positionBeats() const
{
    const auto position = edit_.getTransport().getPosition();
    return edit_.tempoSequence.toBeats(position).inBeats();
}

bool EditClock::isPlaying() const
{
    return edit_.getTransport().isPlaying();
}

double EditClock::displayBeats() const
{
    // An audio block is well under this; a position older than that is a
    // device that stalled, and the playhead waits for it.
    constexpr double longestBlockSeconds = 0.05;

    const auto seconds = edit_.getTransport().getPosition().inSeconds();
    const auto now = juce::Time::getMillisecondCounterHiRes();

    if (!isPlaying())
    {
        seenSeconds_ = -1.0;
        drawnSeconds_ = seconds;
        return positionBeats();
    }

    if (seconds != seenSeconds_)
    {
        seenSeconds_ = seconds;
        seenAtMs_ = now;
    }

    const auto carried = seconds + std::min((now - seenAtMs_) / 1000.0, longestBlockSeconds);

    // Forward only, unless the engine went back further than a block could
    // explain: that is a loop or a jump, and the playhead follows it.
    drawnSeconds_ =
        carried < drawnSeconds_ && drawnSeconds_ - carried < longestBlockSeconds ? drawnSeconds_ : carried;
    return edit_.tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(drawnSeconds_)).inBeats();
}

} // namespace daw::app
