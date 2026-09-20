#include "daw/ui/WorkspaceView.h"

namespace daw::ui
{

WorkspaceView::WorkspaceView(const Tokens& tokens, DawLookAndFeel& lookAndFeel, const PanelRegistry& registry)
    : tokens_(tokens)
    , lookAndFeel_(lookAndFeel)
    , registry_(registry)
{
    setLookAndFeel(&lookAndFeel_);
}

WorkspaceView::~WorkspaceView()
{
    setLookAndFeel(nullptr);
}

void WorkspaceView::show(const WorkspaceManifest& manifest)
{
    panels_.clear();

    layout_ = manifest.layout;
    workspaceId_ = juce::String(manifest.id);

    // The order is the one the layout places them in, so the rectangles that
    // come back from layoutPanels() line up with this list index for index.
    for (const auto& id : manifest.placedPanels())
    {
        PanelContext context{tokens_, lookAndFeel_, id};
        auto panel = registry_.create(context);

        addAndMakeVisible(*panel);
        panels_.push_back(Placed{juce::String(id), std::move(panel)});
    }

    resized();
    repaint();
}

Rect WorkspaceView::surface() const
{
    return Rect{0, 0, getWidth(), getHeight()};
}

LayoutOptions WorkspaceView::options() const
{
    LayoutOptions layoutOptions{};
    layoutOptions.separator = tokens_.integer("stroke.hairline");
    return layoutOptions;
}

void WorkspaceView::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.base"));

    g.setColour(tokens_.colour("color.border.hairline"));
    for (const auto& rule : layoutSeparators(layout_, surface(), options()))
        g.fillRect(rule.x, rule.y, rule.width, rule.height);
}

void WorkspaceView::resized()
{
    const auto placed = layoutPanels(layout_, surface(), options());

    for (std::size_t index = 0; index < panels_.size() && index < placed.size(); ++index)
    {
        const auto& bounds = placed[index].bounds;
        panels_[index].panel->setBounds(bounds.x, bounds.y, bounds.width, bounds.height);
    }
}

} // namespace daw::ui
