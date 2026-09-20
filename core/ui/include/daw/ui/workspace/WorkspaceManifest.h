#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <cstddef>
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

struct WorkspaceManifest
{
    static constexpr std::int64_t supportedSchemaVersion = 1;

    std::string id;
    std::string label;
    std::string description;
    std::vector<std::string> panels;
    LayoutNode layout;
    std::vector<std::string> commands;

    [[nodiscard]] static domain::Result<WorkspaceManifest> parse(std::string_view json);
    [[nodiscard]] static domain::Result<WorkspaceManifest> fromValue(const domain::Value& value);

    // Every panel the layout places, in the order it places them.
    [[nodiscard]] std::vector<std::string> placedPanels() const;
};

} // namespace daw::ui
