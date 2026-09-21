#include "daw/engine/TransportController.h"

namespace daw::engine
{

TransportController::TransportController(tracktion::Edit& edit) noexcept
    : edit_{edit}
{
}

void TransportController::play()
{
    edit_.getTransport().play(false);
}

void TransportController::stop()
{
    edit_.getTransport().stop(false, false);
}

void TransportController::setLoop(bool looping, double startBeats, double endBeats)
{
    auto& control = edit_.getTransport();

    if (looping)
    {
        const tracktion::BeatRange beats{tracktion::BeatPosition::fromBeats(startBeats),
                                         tracktion::BeatPosition::fromBeats(endBeats)};
        control.setLoopRange(edit_.tempoSequence.toTime(beats));
    }

    control.looping = looping;
}

void TransportController::setPosition(double positionBeats)
{
    const auto beats = tracktion::BeatPosition::fromBeats(positionBeats);
    edit_.getTransport().setPosition(edit_.tempoSequence.toTime(beats));
}

} // namespace daw::engine
