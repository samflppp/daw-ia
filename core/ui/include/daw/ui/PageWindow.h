#pragma once

#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui
{

// One page of a windowed workspace: a panel in a window that moves, resizes,
// maximises and closes, over the desktop of the main window — the way FL
// Studio shows its playlist, channel rack and piano roll.
//
// It is a layout host like WorkspaceView, and for the same reason: it places
// the panel it was handed and knows nothing else about it. The panel still
// does not know where it is; it only knows it has a rectangle.
//
// The window does not remember where it goes. The view does, as fractions of
// the desktop, and hands them to the application's settings: a window moved on
// this screen reopens in the same place on the next one.
class PageWindow final : public juce::Component
{
public:
    PageWindow(const Tokens& tokens,
               DawLookAndFeel& lookAndFeel,
               juce::String title,
               std::unique_ptr<juce::Component> panel);
    ~PageWindow() override;

    // The rectangle the window may occupy. Asked for on every move, so a
    // window cannot be dragged under the bar or off the desktop.
    std::function<juce::Rectangle<int>()> desktop;

    std::function<void()> onClose;
    std::function<void()> onMaximise;
    std::function<void()> onFront;

    // Called when the user has moved or resized the window, never when the
    // view placed it: the view stores the first and computes the second.
    std::function<void()> onMoved;

    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    [[nodiscard]] juce::Component& panel() noexcept { return *panel_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    // Keeps the whole window inside the desktop rectangle, whatever the parent
    // is: the bar across the top is part of the parent and not of the desktop.
    class Constrainer final : public juce::ComponentBoundsConstrainer
    {
    public:
        explicit Constrainer(PageWindow& owner)
            : owner_(owner)
        {
        }

        void checkBounds(juce::Rectangle<int>& bounds,
                         const juce::Rectangle<int>& previous,
                         const juce::Rectangle<int>& limits,
                         bool stretchingTop,
                         bool stretchingLeft,
                         bool stretchingBottom,
                         bool stretchingRight) override;

        void resizeEnd() override;

    private:
        PageWindow& owner_;
    };

    [[nodiscard]] juce::Rectangle<int> titleArea() const;
    [[nodiscard]] bool inTitle(const juce::MouseEvent& event) const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    juce::String title_;
    std::unique_ptr<juce::Component> panel_;

    Constrainer constrainer_{*this};
    juce::ComponentDragger dragger_;
    std::unique_ptr<juce::ResizableBorderComponent> border_;

    juce::TextButton maximise_;
    juce::TextButton close_;

    bool active_{false};
    bool dragging_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PageWindow)
};

} // namespace daw::ui
