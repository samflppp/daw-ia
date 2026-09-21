#include "MainWindow.h"

#include <limits>
#include <utility>

namespace daw::app
{

MainWindow::MainWindow(const juce::String& title,
                       const ui::Tokens& tokens,
                       std::unique_ptr<juce::Component> content)
    : juce::DocumentWindow(title, tokens.colour("color.surface.base"), juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(content.release(), false);

    // The tokens say what the interface was drawn for; the display says what
    // it can show. A window larger than the screen is not a big window, it is a
    // window whose right-hand panels nobody can reach — and on a laptop plugged
    // into nothing, that is most of the interface.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto available = display != nullptr ? display->userBounds.toNearestInt()
                                              : juce::Rectangle<int>{tokens.integer("window.default.width"),
                                                                     tokens.integer("window.default.height")};

    const auto width = juce::jmin(tokens.integer("window.default.width"), available.getWidth());
    const auto height = juce::jmin(tokens.integer("window.default.height"), available.getHeight());

    setResizable(true, false);
    setResizeLimits(juce::jmin(tokens.integer("window.minimum.width"), width),
                    juce::jmin(tokens.integer("window.minimum.height"), height),
                    std::numeric_limits<int>::max(),
                    std::numeric_limits<int>::max());
    centreWithSize(width, height);

    setVisible(true);
}

void MainWindow::minimisationStateChanged(bool isNowMinimised)
{
    if (isNowMinimised)
        return;

    repaint();

    if (auto* content = getContentComponent(); content != nullptr)
        content->repaint();
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace daw::app
