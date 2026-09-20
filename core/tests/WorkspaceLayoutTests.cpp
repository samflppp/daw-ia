#include "daw/ui/workspace/LayoutTree.h"
#include "daw/ui/workspace/WorkspaceManifest.h"

#include <ostream>
#include <string>

#include <doctest/doctest.h>

using namespace daw::ui;
using daw::domain::ErrorCode;

namespace
{

// The shape of a real manifest, small enough to reason about: a strip on top,
// and a row of two underneath.
constexpr const char* simpleManifest = R"({
  "schemaVersion": 1,
  "id": "test",
  "label": "Test",
  "panels": ["transport", "tracks", "piano_roll"],
  "layout": {
    "split": "vertical",
    "children": [
      { "panel": "transport", "weight": 1 },
      { "split": "horizontal", "weight": 3,
        "children": [
          { "panel": "tracks", "weight": 1 },
          { "panel": "piano_roll", "weight": 3 }
        ] }
    ]
  }
})";

[[nodiscard]] LayoutNode leaf(std::string name, double weight)
{
    LayoutNode node{};
    node.panel = std::move(name);
    node.weight = weight;
    return node;
}

[[nodiscard]] LayoutNode split(LayoutNode::Split direction, std::vector<LayoutNode> children)
{
    LayoutNode node{};
    node.split = direction;
    node.children = std::move(children);
    return node;
}

} // namespace

TEST_CASE("a manifest is read into a tree, and the tree names its panels in order")
{
    auto manifest = WorkspaceManifest::parse(simpleManifest);
    REQUIRE(manifest.ok());

    CHECK(manifest.value().id == "test");
    CHECK(manifest.value().label == "Test");

    const auto placed = manifest.value().placedPanels();
    REQUIRE(placed.size() == 3);
    CHECK(placed[0] == "transport");
    CHECK(placed[1] == "tracks");
    CHECK(placed[2] == "piano_roll");
}

TEST_CASE("a layout that places a panel the manifest never declared is refused")
{
    const auto text = std::string{R"({"schemaVersion":1,"id":"test","label":"Test",)"} +
                      R"("panels":["transport"],"layout":{"panel":"mixer"}})";

    auto manifest = WorkspaceManifest::parse(text);
    REQUIRE(!manifest.ok());
    CHECK(manifest.error().code == ErrorCode::invalidPayload);
}

TEST_CASE("a panel that is declared but never placed is refused")
{
    const auto text = std::string{R"({"schemaVersion":1,"id":"test","label":"Test",)"} +
                      R"("panels":["transport","mixer"],"layout":{"panel":"transport"}})";

    auto manifest = WorkspaceManifest::parse(text);
    REQUIRE(!manifest.ok());
    CHECK(manifest.error().code == ErrorCode::invalidPayload);
}

TEST_CASE("a node that is both a panel and a split is refused, not guessed at")
{
    const auto text = std::string{R"({"schemaVersion":1,"id":"test","label":"Test","panels":["a"],)"} +
                      R"("layout":{"panel":"a","split":"vertical","children":[]}})";

    auto manifest = WorkspaceManifest::parse(text);
    REQUIRE(!manifest.ok());
    CHECK(manifest.error().code == ErrorCode::invalidPayload);
}

TEST_CASE("a manifest from a schema this build does not know is refused whole")
{
    const auto text = std::string{R"({"schemaVersion":2,"id":"test","label":"Test","panels":["a"],)"} +
                      R"("layout":{"panel":"a"}})";

    auto manifest = WorkspaceManifest::parse(text);
    REQUIRE(!manifest.ok());
    CHECK(manifest.error().code == ErrorCode::invalidPayload);
}

TEST_CASE("weights divide the surface, and the parts cover it exactly")
{
    const auto root = split(LayoutNode::Split::horizontal,
                            {leaf("a", 1.0), leaf("b", 1.0), leaf("c", 1.0)});

    // 1000 over three is not an integer, which is exactly the case that leaves
    // a gap when each part is rounded on its own.
    const auto panels = layoutPanels(root, Rect{0, 0, 1000, 400});

    REQUIRE(panels.size() == 3);
    CHECK(panels[0].bounds.x == 0);
    CHECK(panels[2].bounds.right() == 1000);

    for (std::size_t index = 1; index < panels.size(); ++index)
        CHECK(panels[index].bounds.x == panels[index - 1].bounds.right());
}

TEST_CASE("a vertical split stacks, a horizontal split puts side by side")
{
    const auto vertical = split(LayoutNode::Split::vertical, {leaf("a", 1.0), leaf("b", 1.0)});
    const auto stacked = layoutPanels(vertical, Rect{0, 0, 200, 100});
    CHECK(stacked[0].bounds == Rect{0, 0, 200, 50});
    CHECK(stacked[1].bounds == Rect{0, 50, 200, 50});

    const auto horizontal = split(LayoutNode::Split::horizontal, {leaf("a", 1.0), leaf("b", 1.0)});
    const auto side = layoutPanels(horizontal, Rect{0, 0, 200, 100});
    CHECK(side[0].bounds == Rect{0, 0, 100, 100});
    CHECK(side[1].bounds == Rect{100, 0, 100, 100});
}

TEST_CASE("separators are cut out of the surface, never painted over a panel")
{
    const auto root = split(LayoutNode::Split::horizontal, {leaf("a", 1.0), leaf("b", 1.0)});

    LayoutOptions options{};
    options.separator = 2;

    const auto panels = layoutPanels(root, Rect{0, 0, 102, 50}, options);
    const auto rules = layoutSeparators(root, Rect{0, 0, 102, 50}, options);

    CHECK(panels[0].bounds == Rect{0, 0, 50, 50});
    CHECK(panels[1].bounds == Rect{52, 0, 50, 50});

    REQUIRE(rules.size() == 1);
    CHECK(rules[0] == Rect{50, 0, 2, 50});

    // No panel and no rule overlap: the sum is the surface.
    CHECK(panels[0].bounds.width + rules[0].width + panels[1].bounds.width == 102);
}

TEST_CASE("a surface too small for its separators yields nothing, never a negative size")
{
    const auto root = split(LayoutNode::Split::horizontal, {leaf("a", 1.0), leaf("b", 1.0)});

    LayoutOptions options{};
    options.separator = 8;

    const auto panels = layoutPanels(root, Rect{0, 0, 4, 20}, options);
    REQUIRE(panels.size() == 2);
    for (const auto& panel : panels)
    {
        CHECK(panel.bounds.width >= 0);
        CHECK(panel.bounds.isEmpty());
    }
}

TEST_CASE("the nested manifest lands where its weights say")
{
    auto manifest = WorkspaceManifest::parse(simpleManifest);
    REQUIRE(manifest.ok());

    const auto panels = layoutPanels(manifest.value().layout, Rect{0, 0, 800, 400});
    REQUIRE(panels.size() == 3);

    // transport takes a quarter of the height, and the whole width.
    CHECK(panels[0].bounds == Rect{0, 0, 800, 100});

    // tracks and piano_roll share the rest, one to three.
    CHECK(panels[1].bounds == Rect{0, 100, 200, 300});
    CHECK(panels[2].bounds == Rect{200, 100, 600, 300});
}
