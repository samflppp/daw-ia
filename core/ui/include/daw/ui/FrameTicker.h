#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace daw::ui
{

// One call per image the screen shows, for what moves on its own: the
// playhead, the transport's readout, the meters (S18 bis).
//
// A timer of 33 ms is not an image: at 60 Hz it lands on one image in two and
// on the next one, at 144 Hz on one in four or five, and the playhead goes by
// jolts. This is called at the vertical blank of the display the component is
// on, so a playhead moves once per image, whatever the display. Nothing is
// called while the component is not on a screen.
//
// What the callback repaints is still its business: the playhead repaints the
// columns it leaves and reaches, a meter only when its value changed.
class FrameTicker final
{
public:
    FrameTicker(juce::Component& owner, std::function<void()> onFrame);

private:
    juce::VBlankAttachment vblank_;

    JUCE_DECLARE_NON_COPYABLE(FrameTicker)
};

} // namespace daw::ui
