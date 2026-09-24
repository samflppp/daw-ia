#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::ui
{

// A workspace manifest, as workspaces/*.json declares it.
//
// This file knows nothing of JUCE, and that is the point. A manifest is data:
// which panels exist, how they divide the surface, which commands the workspace
// exposes. Parsing it and turning weights into rectangles is arithmetic, and
// arithmetic that needs a window to be tested is arithmetic nobody tests.
//
// The schema is workspaces/schema/workspace.schema.json and CI validates every
// manifest against it. What is checked here is what the schema cannot say: that
// a layout names no panel the manifest did not declare, and that the panel list
// has no orphan.

struct LayoutNode
{
    enum class Split
    {
        // The children are stacked one under the other: a vertical split cuts
        // the surface with horizontal lines. This is the reading the schema
        // uses, and the one every editor uses; the opposite convention exists
        // and costs an hour of confusion every time it is met.
        vertical,
        horizontal
    };

    // A leaf names a panel. A branch names a split and holds children. Never
    // both, and the parser refuses anything else rather than guessing.
    std::string panel;
    Split split{Split::vertical};
    double weight{1.0};
    std::vector<LayoutNode> children;

    [[nodiscard]] bool isLeaf() const noexcept { return children.empty(); }
};

// One page of a windowed workspace: a panel in a window of its own, the way
// FL Studio shows its playlist, channel rack and piano roll.
//
// Where it opens is a fraction of the desktop, never a pixel: a manifest holds
// no visual value, and the same manifest has to open on a laptop and on a
// wide screen. Where the user has since moved it is a setting of the machine,
// kept by the application — not a property of the manifest, and not of the
// project.
struct Page
{
    std::string panel;
    std::string title;

    // "F5" … "F12", or empty. The key opens the page, brings it to the front,
    // or closes it when it is already in front.
    std::string shortcut;

    bool open{true};
    double x{0.0};
    double y{0.0};
    double width{0.5};
    double height{0.5};
};

// A workspace made of windows: a bar across the top that is always there, and
// pages over a desktop underneath.
struct WindowedLayout
{
    std::vector<std::string> bar;
    std::vector<Page> pages;
};

struct WorkspaceManifest
{
    static constexpr std::int64_t supportedSchemaVersion = 1;

    std::string id;
    std::string label;
    std::string description;
    std::vector<std::string> panels;
    LayoutNode layout;

    // Set when the layout is made of windows rather than of splits. The split
    // tree is then empty, and every panel is either on the bar or on a page.
    std::optional<WindowedLayout> windows;

    std::vector<std::string> commands;

    [[nodiscard]] static domain::Result<WorkspaceManifest> parse(std::string_view json);
    [[nodiscard]] static domain::Result<WorkspaceManifest> fromValue(const domain::Value& value);

    // Every panel the layout places, in the order it places them: the bar,
    // then the pages, for a windowed layout.
    [[nodiscard]] std::vector<std::string> placedPanels() const;
};

} // namespace daw::ui
