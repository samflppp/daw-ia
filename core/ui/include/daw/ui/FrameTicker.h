#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui
{

// One call per image the screen shows, for what moves on its own: the
// playhead, the transport's readout, the meters (S18 bis).
//
// A timer of 33 ms is not an image: at 60 Hz it lands on one image in two and
// on the next one, at 144 Hz on one in four or five, and the playhead goes by
// jolts. On the fluid pace this is called at the vertical blank of the display
// the component is on, so a playhead moves once per image, whatever the
// display. On the light pace, for a machine whose graphics struggle, it is a
// timer at the rate the tokens give (motion.light.framesPerSecond). Nothing is
// called while the component is not on a screen.
//
// The pace is the machine's, not the project's: set once for the whole
// interface, from the application's settings, and followed at once by every
// ticker alive.
//
// What the callback repaints is still its business: the playhead repaints the
// columns it leaves and reaches, a meter only when its value changed.
class FrameTicker final : private juce::Timer
{
public:
    enum class Pace
    {
        fluid, // in step with the display
        light  // a fixed, lower rate
    };

    FrameTicker(juce::Component& owner, std::function<void()> onFrame);
    ~FrameTicker() override;

    static void setPace(Pace pace);
    [[nodiscard]] static Pace pace() noexcept;

private:
    void timerCallback() override;
    void follow(Pace pace);

    juce::Component& owner_;
    std::function<void()> onFrame_;
    std::unique_ptr<juce::VBlankAttachment> vblank_;

    JUCE_DECLARE_NON_COPYABLE(FrameTicker)
};

} // namespace daw::ui
