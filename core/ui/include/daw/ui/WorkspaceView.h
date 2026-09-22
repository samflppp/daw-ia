#pragma once

#include "daw/ui/PanelRegistry.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/workspace/LayoutTree.h"
#include "daw/ui/workspace/WorkspaceManifest.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui
{

// The screen, built from a manifest.
//
// Hygiene rule 2 has exactly one exception in the whole interface and this is
// it: WorkspaceView is the only component that calls setBounds on a panel. The
// panels it holds are components it was handed by the registry; it knows their
// identifiers and their rectangles, and nothing else about them.
//
// The separators are painted here for the same reason. A rule drawn by a panel
// is a rule that knows it has a neighbour.
class WorkspaceView final : public juce::Component
{
public:
    WorkspaceView(const PanelServices& services, const PanelRegistry& registry);
    ~WorkspaceView() override;

    // Tears down the panels of the previous workspace and builds the new ones.
    // Switching workspace is exactly this call: the manifest is the screen.
    void show(const WorkspaceManifest& manifest);

    [[nodiscard]] const juce::String& workspaceId() const noexcept { return workspaceId_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Ctrl+Z and Ctrl+Y live here and not in a panel, for the reason hygiene
    // rule 2 exists: undo belongs to the project, not to whatever has the
    // focus. JUCE hands an unhandled key press up the parent chain, so this
    // is the last component to see it — which is exactly where a shortcut
    // that must work everywhere belongs.
    //
    // A text field that handles its own Ctrl+Z keeps it: typing a request to
    // the copilot and undoing a word must not undo an edit of the project.
    bool keyPressed(const juce::KeyPress& key) override;

private:
    struct Placed
    {
        juce::String id;
        std::unique_ptr<juce::Component> panel;
    };

    [[nodiscard]] Rect surface() const;
    [[nodiscard]] LayoutOptions options() const;

    PanelServices services_;
    const PanelRegistry& registry_;

    LayoutNode layout_;
    juce::String workspaceId_;
    std::vector<Placed> panels_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceView)
};

} // namespace daw::ui
