#pragma once

#include "daw/ui/PageWindow.h"
#include "daw/ui/PanelRegistry.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/workspace/LayoutTree.h"
#include "daw/ui/workspace/WorkspaceManifest.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

namespace daw::ui
{

// The screen, built from a manifest.
//
// Hygiene rule 2 has exactly two exceptions in the whole interface: this view
// and the PageWindow it creates are the only components that call setBounds on
// a panel. The panels they hold are components they were handed by the
// registry; they know their identifiers and their rectangles, and nothing
// else about them.
//
// A manifest is laid out one of two ways:
//
//   splits   the surface is cut into panels by the layout tree, and the
//            separators are painted here — a rule drawn by a panel is a rule
//            that knows it has a neighbour;
//   windows  a bar across the top, a row of page tabs under it, and pages in
//            windows over the desktop underneath, the way FL Studio shows its
//            playlist, channel rack and piano roll. F5, F6 … open a page,
//            bring it to the front, or close it when it is already there.
class WorkspaceView final : public juce::Component
{
public:
    WorkspaceView(const PanelServices& services, const PanelRegistry& registry);
    ~WorkspaceView() override;

    // Where pages are remembered between two launches: open or closed, and
    // their place as fractions of the desktop. Optional; without it every
    // launch opens the pages where the manifest says. Not the project: where a
    // window sits is a matter of this machine's screen.
    void setPageMemory(juce::PropertySet* memory) noexcept { memory_ = memory; }

    // Tears down the panels of the previous workspace and builds the new ones.
    // Switching workspace is exactly this call: the manifest is the screen.
    void show(const WorkspaceManifest& manifest);

    [[nodiscard]] const juce::String& workspaceId() const noexcept { return workspaceId_; }

    // Opens a page and brings it to the front, or closes it. The same thing
    // the tabs and the function keys do. False when the workspace has no such
    // page.
    bool showPage(std::string_view panel, bool visible);

    // The panel of a page or of the bar, by identifier: what a scripted
    // verification drives. Null when this workspace does not hold it.
    [[nodiscard]] juce::Component* panel(std::string_view id) const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Ctrl+Z and Ctrl+Y live here and not in a panel, for the reason hygiene
    // rule 2 exists: undo belongs to the project, not to whatever has the
    // focus. JUCE hands an unhandled key press up the parent chain, so this
    // is the last component to see it — which is exactly where a shortcut
    // that must work everywhere belongs. The page keys live here for the same
    // reason.
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

    // One page: what the manifest said, where it is now, and its window.
    struct PageSlot
    {
        Page page;
        bool open{true};
        bool maximised{false};
        PageFractions place{};
        std::unique_ptr<PageWindow> window;
        std::unique_ptr<juce::TextButton> tab;
    };

    [[nodiscard]] Rect surface() const;
    [[nodiscard]] LayoutOptions options() const;

    // --- windows
    [[nodiscard]] bool windowed() const noexcept { return !pages_.empty(); }
    [[nodiscard]] juce::Rectangle<int> barArea() const;
    [[nodiscard]] juce::Rectangle<int> tabArea() const;
    [[nodiscard]] juce::Rectangle<int> desktopArea() const;

    void buildPages(const WindowedLayout& layout);
    void placePage(PageSlot& slot);
    void setOpen(PageSlot& slot, bool open);
    void bringToFront(PageSlot& slot);
    void markActive(const PageSlot* front);
    void remember(const PageSlot& slot) const;
    void recall(PageSlot& slot) const;
    [[nodiscard]] PageSlot* slotFor(std::string_view panel);

    PanelServices services_;
    const PanelRegistry& registry_;
    juce::PropertySet* memory_{nullptr};

    LayoutNode layout_;
    juce::String workspaceId_;
    std::vector<Placed> panels_;
    std::vector<PageSlot> pages_;

    // True while this view is placing windows itself: a window it moves is not
    // a window the user moved, and must not be remembered as one.
    bool placing_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkspaceView)
};

} // namespace daw::ui
