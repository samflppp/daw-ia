#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <functional>

namespace daw::ui
{

// A slider whose right-click opens its automation line, the way FL's
// "Create automation clip" does. Every other click is the slider's own.
//
// The right button never drags: JUCE would otherwise move the value under a
// hand that only asked for the line.
class AutomatableSlider final : public juce::Slider
{
public:
    std::function<void()> onAutomate;

    // Shows a value the panel read (the project, or the curve while the song
    // plays) without sending it back. The hand wins: while a button is down on
    // the slider, it shows the hand. True when the slider moved.
    bool follow(double value)
    {
        if (isMouseButtonDown() || std::abs(getValue() - value) < 1.0e-6)
            return false;
        setValue(value, juce::dontSendNotification);
        return true;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            rightDown_ = true;
            if (onAutomate)
                onAutomate();
            return;
        }
        rightDown_ = false;
        juce::Slider::mouseDown(event);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!rightDown_)
            juce::Slider::mouseDrag(event);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (!rightDown_)
            juce::Slider::mouseUp(event);
        rightDown_ = false;
    }

private:
    bool rightDown_{false};
};

} // namespace daw::ui
