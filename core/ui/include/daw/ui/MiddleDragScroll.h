#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui
{

// The middle button held down drags what a viewport shows, the way it drags
// the playlist, the canvas and the piano roll (S18): one gesture moves what
// scrolls, in every panel that scrolls. The other buttons are left to the
// components inside, which never see the middle one move them.
class MiddleDragScroll final : private juce::MouseListener
{
public:
    explicit MiddleDragScroll(juce::Viewport& viewport)
        : viewport_(viewport)
    {
        viewport_.addMouseListener(this, true);
    }

    ~MiddleDragScroll() override { viewport_.removeMouseListener(this); }

    MiddleDragScroll(const MiddleDragScroll&) = delete;
    MiddleDragScroll& operator=(const MiddleDragScroll&) = delete;
    MiddleDragScroll(MiddleDragScroll&&) = delete;
    MiddleDragScroll& operator=(MiddleDragScroll&&) = delete;

private:
    void mouseDown(const juce::MouseEvent& event) override
    {
        dragging_ = event.mods.isMiddleButtonDown();
        if (!dragging_)
            return;
        start_ = event.getEventRelativeTo(&viewport_).getPosition();
        origin_ = viewport_.getViewPosition();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragging_)
            return;
        const auto moved = event.getEventRelativeTo(&viewport_).getPosition() - start_;
        viewport_.setViewPosition(origin_ - moved);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        juce::ignoreUnused(event);
        dragging_ = false;
    }

    juce::Viewport& viewport_;
    juce::Point<int> start_;
    juce::Point<int> origin_;
    bool dragging_{false};
};

} // namespace daw::ui
