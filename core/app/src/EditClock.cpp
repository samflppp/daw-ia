#include "EditClock.h"

#include <algorithm>
#include <utility>

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

void EditClock::setOutputNotice(std::string notice, bool lost)
{
    outputNotice_ = std::move(notice);
    outputLost_ = lost;
    outputNoticeAtMs_ = juce::Time::getMillisecondCounterHiRes();
}

std::string EditClock::outputNotice() const
{
    // A lost output is said until one opens; where the sound went, for as
    // long as it takes to read it.
    constexpr double shownMs = 6000.0;
    if (outputLost_ || juce::Time::getMillisecondCounterHiRes() - outputNoticeAtMs_ < shownMs)
        return outputNotice_;
    return {};
}

} // namespace daw::app
