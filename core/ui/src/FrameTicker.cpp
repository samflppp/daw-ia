#include "daw/ui/FrameTicker.h"

#include <utility>

namespace daw::ui
{

FrameTicker::FrameTicker(juce::Component& owner, std::function<void()> onFrame)
    : vblank_(&owner, std::move(onFrame))
{
}

} // namespace daw::ui
