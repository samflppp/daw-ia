#pragma once

#include "daw/ui/model/TransportClock.h"

#include <tracktion_engine/tracktion_engine.h>

namespace daw::app
{

// The playhead, read from the Edit.
//
// This class exists so that core/ui never includes Tracktion. The interface it
// answers asks two questions; everything Tracktion-shaped about them stops
// here.
class EditClock final : public ui::TransportClock
{
public:
    explicit EditClock(tracktion::Edit& edit) noexcept;

    [[nodiscard]] double positionBeats() const override;
    [[nodiscard]] bool isPlaying() const override;

    // Between two audio blocks the position is carried forward by the time
    // that passed, never more than one block's worth and never backwards,
    // unless the engine itself jumped back: a loop, a click on the ruler.
    [[nodiscard]] double displayBeats() const override;

private:
    tracktion::Edit& edit_;

    // The engine's position last seen, when it was first seen, and what was
    // last drawn, in seconds of the Edit.
    mutable double seenSeconds_{-1.0};
    mutable double seenAtMs_{0.0};
    mutable double drawnSeconds_{0.0};
};

} // namespace daw::app
