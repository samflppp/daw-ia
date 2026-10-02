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
    const auto seconds = edit_.getTransport().getPosition().inSeconds();
    if (!isPlaying())
    {
        drawn_.stop();
        return positionBeats();
    }

    const auto drawn = drawn_.advance(seconds, juce::Time::getMillisecondCounterHiRes());
    return edit_.tempoSequence.toBeats(tracktion::TimePosition::fromSeconds(drawn)).inBeats();
}

} // namespace daw::app
