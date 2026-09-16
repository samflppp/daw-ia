#include "daw/ui/RootView.h"

namespace daw::ui
{

RootView::RootView(const Tokens& tokens)
    : tokens_(tokens)
{
}

void RootView::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.base"));
}

void RootView::resized()
{
    // No panels yet. Layout will be computed from the active workspace manifest.
}

} // namespace daw::ui
