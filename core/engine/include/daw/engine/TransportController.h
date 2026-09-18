#pragma once

#include "daw/domain/project/ProjectState.h"

#include <tracktion_engine/tracktion_engine.h>

namespace daw::engine
{

// Maps the domain's TransportState onto Tracktion's TransportControl.
//
// It remembers what it last pushed, so a reconciliation that changes nothing
// about the transport issues no call at all — restarting playback on every
// fader frame would be audible.
class TransportController
{
public:
    explicit TransportController(tracktion::Edit& edit) noexcept;

    void apply(const domain::TransportState& transport);

private:
    tracktion::Edit& edit_;
    bool playing_{false};
    double positionBeats_{0.0};
    bool everApplied_{false};
};

} // namespace daw::engine
