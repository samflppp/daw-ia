#include "daw/engine/TransportController.h"

namespace daw::engine
{

TransportController::TransportController(tracktion::Edit& edit) noexcept
    : edit_{edit}
{
}

void TransportController::apply(const domain::TransportState& transport)
{
    auto& control = edit_.getTransport();

    const bool positionChanged = !everApplied_ || transport.positionBeats != positionBeats_;
    const bool playingChanged = !everApplied_ || transport.playing != playing_;

    if (positionChanged)
    {
        const auto beats = tracktion::BeatPosition::fromBeats(transport.positionBeats);
        control.setPosition(edit_.tempoSequence.toTime(beats));
    }

    if (playingChanged)
    {
        if (transport.playing)
            control.play(false);
        else
            control.stop(false, false);
    }

    playing_ = transport.playing;
    positionBeats_ = transport.positionBeats;
    everApplied_ = true;
}

} // namespace daw::engine
