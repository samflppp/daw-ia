#include "MainWindow.h"

#include "daw/ui/RootView.h"

#include <limits>

namespace daw::app
{

MainWindow::MainWindow(const juce::String& title, const ui::Tokens& tokens)
    : juce::DocumentWindow(title, tokens.colour("color.surface.base"), juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new ui::RootView(tokens), false);

    setResizable(true, false);
    setResizeLimits(tokens.integer("window.minimum.width"),
                    tokens.integer("window.minimum.height"),
                    std::numeric_limits<int>::max(),
                    std::numeric_limits<int>::max());
    centreWithSize(tokens.integer("window.default.width"), tokens.integer("window.default.height"));

    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace daw::app
