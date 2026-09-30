#pragma once

#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace daw::app
{

class MainWindow final : public juce::DocumentWindow
{
public:
    // The window owns what it shows and knows nothing else about it: the
    // application decides whether that is the workspace or the style gallery.
    //
    // With ownTitleBar, the system's title bar is gone: the content draws its
    // own, with the File menu and the window buttons, and moves the window.
    MainWindow(const juce::String& title,
               const ui::Tokens& tokens,
               std::unique_ptr<juce::Component> content,
               bool ownTitleBar);

    void closeButtonPressed() override;

    // With its own title bar, the window still has a system caption and system
    // buttons (S18 bis): Windows asks what is under the pointer, the
    // component under it answers — the title bar says caption or which
    // button — and the system moves the window, snaps it to the edges and
    // maximises it on a double-click. A setBounds per mouse move, as the
    // ComponentDragger did, is what made the window drag behind the hand.
    [[nodiscard]] WindowControlKind findControlAtPoint(juce::Point<float> point) const override;
    [[nodiscard]] int getDesktopWindowStyleFlags() const override;

    // Restoring from the taskbar was reported as coming back to a white, frozen
    // window. It could not be reproduced here in nine cycles, with and without
    // a plugin window open, so this is not a fix for a diagnosed cause: it is
    // the one thing a host can do about a window that came back without being
    // asked to paint. If the report survives it, the next suspect is the
    // Direct2D renderer JUCE 8 uses by default on Windows.
    void minimisationStateChanged(bool isNowMinimised) override;

private:
    bool ownTitleBar_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace daw::app
