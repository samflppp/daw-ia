#include "MainWindow.h"

#include <limits>
#include <utility>
#include <vector>

namespace daw::app
{

MainWindow::MainWindow(const juce::String& title,
                       const ui::Tokens& tokens,
                       std::unique_ptr<juce::Component> content,
                       bool ownTitleBar)
    : juce::DocumentWindow(
          title, tokens.colour("color.surface.base"), ownTitleBar ? 0 : juce::DocumentWindow::allButtons)
    , ownTitleBar_(ownTitleBar)
{
    setUsingNativeTitleBar(!ownTitleBar);
    if (ownTitleBar)
        setTitleBarHeight(0);
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

auto MainWindow::findControlAtPoint(juce::Point<float> point) const -> WindowControlKind
{
    // The resizing edges first, as the document window finds them.
    if (const auto edge = juce::DocumentWindow::findControlAtPoint(point); edge != WindowControlKind::client)
        return edge;

    if (!ownTitleBar_)
        return WindowControlKind::client;

    // Down to the deepest component under the point, by rectangles only: the
    // system hit-test must not be asked from inside the system hit-test.
    std::vector<std::pair<const juce::Component*, juce::Point<float>>> path{{this, point}};
    for (;;)
    {
        const auto& [parent, local] = path.back();
        const juce::Component* found = nullptr;
        for (int index = parent->getNumChildComponents(); --index >= 0;)
        {
            const auto* child = parent->getChildComponent(index);
            if (child->isVisible() && child->getBounds().toFloat().contains(local))
            {
                found = child;
                break;
            }
        }
        if (found == nullptr)
            break;
        path.emplace_back(found, local - found->getPosition().toFloat());
    }

    // Then back up: the first component that is more than client says so.
    for (auto step = path.rbegin(); step != path.rend(); ++step)
    {
        if (step->first == this)
            break;
        if (const auto kind = step->first->findControlAtPoint(step->second);
            kind != WindowControlKind::client)
            return kind;
    }
    return WindowControlKind::client;
}

int MainWindow::getDesktopWindowStyleFlags() const
{
    auto style = juce::DocumentWindow::getDesktopWindowStyleFlags();
    if (ownTitleBar_)
        style |= juce::ComponentPeer::windowHasMinimiseButton | juce::ComponentPeer::windowHasMaximiseButton |
                 juce::ComponentPeer::windowHasCloseButton;
    return style;
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace daw::app
