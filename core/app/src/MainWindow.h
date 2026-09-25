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

    // Restoring from the taskbar was reported as coming back to a white, frozen
    // window. It could not be reproduced here in nine cycles, with and without
    // a plugin window open, so this is not a fix for a diagnosed cause: it is
    // the one thing a host can do about a window that came back without being
    // asked to paint. If the report survives it, the next suspect is the
    // Direct2D renderer JUCE 8 uses by default on Windows.
    void minimisationStateChanged(bool isNowMinimised) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace daw::app
