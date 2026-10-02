#pragma once

#include "daw/ui/model/Motion.h"
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

    // Between two audio blocks the playhead moves on with the time that
    // passed, pulled towards the engine's position: see DrawnPlayhead.
    [[nodiscard]] double displayBeats() const override;

private:
    tracktion::Edit& edit_;

    mutable ui::DrawnPlayhead drawn_;
};

} // namespace daw::app
