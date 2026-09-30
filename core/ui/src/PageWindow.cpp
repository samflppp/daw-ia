#include "daw/ui/PageWindow.h"

#include <utility>

namespace daw::ui
{
namespace
{

// Every edge has to stay on the desktop: a window whose title bar has left the
// screen is a window that cannot be moved back or closed.
constexpr int wholeWindow = 1 << 20;

} // namespace

void PageWindow::Constrainer::checkBounds(juce::Rectangle<int>& bounds,
                                          const juce::Rectangle<int>& previous,
                                          const juce::Rectangle<int>& limits,
                                          bool stretchingTop,
                                          bool stretchingLeft,
                                          bool stretchingBottom,
                                          bool stretchingRight)
{
    const auto area = owner_.desktop ? owner_.desktop() : limits;
    ComponentBoundsConstrainer::checkBounds(
        bounds, previous, area, stretchingTop, stretchingLeft, stretchingBottom, stretchingRight);
}

void PageWindow::Constrainer::resizeEnd()
{
    if (owner_.onMoved)
        owner_.onMoved();
}

PageWindow::PageWindow(const Tokens& tokens,
                       DawLookAndFeel& lookAndFeel,
                       juce::String title,
                       std::unique_ptr<juce::Component> panel)
    : tokens_(tokens)
    , lookAndFeel_(lookAndFeel)
    , title_(std::move(title))
    , panel_(std::move(panel))
{
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true); // paint() fills the whole rectangle: what is behind is never painted

    addAndMakeVisible(*panel_);

    // A click anywhere in the page brings it to the front, the way a window
    // does: the panel's own clicks are heard here too, and passed on untouched.
    panel_->addMouseListener(this, true);

    constrainer_.setMinimumSize(tokens_.integer("metric.page.minWidth"),
                                tokens_.integer("metric.page.minHeight"));
    constrainer_.setMinimumOnscreenAmounts(wholeWindow, wholeWindow, wholeWindow, wholeWindow);

    // Added after the panel so it sits above it; it only answers on its edges.
    border_ = std::make_unique<juce::ResizableBorderComponent>(this, &constrainer_);
    border_->setBorderThickness(juce::BorderSize<int>{tokens_.integer("metric.page.border")});
    addAndMakeVisible(*border_);

    maximise_.setButtonText(juce::String::fromUTF8("\xe2\x96\xa1")); // □
    maximise_.setTooltip("Agrandir");
    maximise_.onClick = [this]
    {
        if (onMaximise)
            onMaximise();
    };

    close_.setButtonText(juce::String::fromUTF8("\xc3\x97")); // ×
    close_.setTooltip("Fermer");
    close_.onClick = [this]
    {
        if (onClose)
            onClose();
    };

    addAndMakeVisible(maximise_);
    addAndMakeVisible(close_);
}

PageWindow::~PageWindow()
{
    panel_->removeMouseListener(this);
    setLookAndFeel(nullptr);
}

void PageWindow::setActive(bool active)
{
    if (active_ == active)
        return;

    active_ = active;
    repaint(titleArea());
}

juce::Rectangle<int> PageWindow::titleArea() const
{
    return getLocalBounds().removeFromTop(tokens_.integer("metric.page.titleHeight"));
}

bool PageWindow::inTitle(const juce::MouseEvent& event) const
{
    return event.eventComponent == this && titleArea().contains(event.getPosition());
}

void PageWindow::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.panel"));

    auto title = titleArea();
    g.setColour(tokens_.colour(active_ ? "color.surface.raised" : "color.surface.sunken"));
    g.fillRect(title);

    // The window in front is marked by a line in the accent colour, and only
    // that one: with several pages open, the eye has to find where the keys go.
    if (active_)
    {
        g.setColour(tokens_.colour("color.accent.primary"));
        g.fillRect(title.removeFromTop(tokens_.integer("stroke.hairline")));
    }

    auto text = titleArea();
    text.removeFromLeft(tokens_.integer("space.md"));
    g.setColour(tokens_.colour(active_ ? "color.text.primary" : "color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText(title_, text, juce::Justification::centredLeft, true);

    g.setColour(tokens_.colour("color.border.strong"));
    g.drawRect(getLocalBounds(), tokens_.integer("stroke.hairline"));
}

void PageWindow::resized()
{
    auto area = getLocalBounds();
    auto title = area.removeFromTop(tokens_.integer("metric.page.titleHeight"));

    const auto button = tokens_.integer("metric.page.buttonWidth");
    close_.setBounds(title.removeFromRight(button));
    maximise_.setBounds(title.removeFromRight(button));

    panel_->setBounds(area.reduced(tokens_.integer("stroke.hairline")));
    border_->setBounds(getLocalBounds());
}

void PageWindow::mouseDown(const juce::MouseEvent& event)
{
    toFront(false);
    if (onFront)
        onFront();

    dragging_ = inTitle(event);
    if (dragging_)
        dragger_.startDraggingComponent(this, event);
}

void PageWindow::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging_)
        dragger_.dragComponent(this, event, &constrainer_);
}

void PageWindow::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (!dragging_)
        return;

    dragging_ = false;
    if (onMoved)
        onMoved();
}

void PageWindow::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (inTitle(event) && onMaximise)
        onMaximise();
}

} // namespace daw::ui
