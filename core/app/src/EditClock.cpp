#include "EditClock.h"

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

} // namespace daw::app
