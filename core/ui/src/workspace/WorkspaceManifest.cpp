#include "daw/ui/workspace/WorkspaceManifest.h"

#include "daw/domain/serialization/Json.h"

#include <algorithm>

namespace daw::ui
{
namespace
{

using domain::ErrorCode;
using domain::fail;
using domain::Result;
using domain::Value;

Result<std::vector<std::string>> stringsAt(const Value& value, std::string_view key, bool required)
{
    const auto* found = value.find(key);
    if (found == nullptr)
    {
        if (required)
            return fail(ErrorCode::invalidPayload, std::string{key} + " is missing");
        return std::vector<std::string>{};
    }

    const auto* items = found->asArray();
    if (items == nullptr)
        return fail(ErrorCode::invalidPayload, std::string{key} + " must be an array");

    std::vector<std::string> strings;
    strings.reserve(items->size());
    for (const auto& item : *items)
    {
        auto text = item.asString();
        if (!text)
            return fail(ErrorCode::invalidPayload, std::string{key} + " must hold strings");
        strings.push_back(text.value());
    }
    return strings;
}

Result<LayoutNode> nodeFromValue(const Value& value)
{
    const auto* panel = value.find("panel");
    const auto* split = value.find("split");

    if ((panel == nullptr) == (split == nullptr))
        return fail(ErrorCode::invalidPayload, "a layout node is either a panel or a split, never both");

    LayoutNode node{};

    if (const auto* weight = value.find("weight"); weight != nullptr)
    {
        auto number = weight->asDouble();
        if (!number)
            return number.error();
        if (number.value() <= 0.0)
            return fail(ErrorCode::invalidPayload, "a weight is strictly positive");
        node.weight = number.value();
    }

    if (panel != nullptr)
    {
        auto name = panel->asString();
        if (!name)
            return name.error();
        if (name.value().empty())
            return fail(ErrorCode::invalidPayload, "a panel has a name");
        node.panel = name.value();
        return node;
    }

    auto direction = split->asString();
    if (!direction)
        return direction.error();

    if (direction.value() == "vertical")
        node.split = LayoutNode::Split::vertical;
    else if (direction.value() == "horizontal")
        node.split = LayoutNode::Split::horizontal;
    else
        return fail(ErrorCode::invalidPayload, "unknown split: " + direction.value());

    const auto* children = value.find("children");
    if (children == nullptr)
        return fail(ErrorCode::invalidPayload, "a split has children");

    const auto* items = children->asArray();
    if (items == nullptr)
        return fail(ErrorCode::invalidPayload, "children must be an array");

    // Two is the schema's minimum and it is not arbitrary: a split with one
    // child is the child, and writing it as a split hides a mistake.
    if (items->size() < 2)
        return fail(ErrorCode::invalidPayload, "a split holds at least two children");

    node.children.reserve(items->size());
    for (const auto& item : *items)
    {
        auto child = nodeFromValue(item);
        if (!child)
            return child.error();
        node.children.push_back(std::move(child).value());
    }

    return node;
}

Result<double> fractionAt(const Value& value, std::string_view key, double fallback)
{
    const auto* found = value.find(key);
    if (found == nullptr)
        return fallback;

    auto number = found->asDouble();
    if (!number)
        return number.error();

    if (number.value() < 0.0 || number.value() > 1.0)
        return fail(ErrorCode::invalidPayload,
                    std::string{key} + " is a fraction of the desktop, between 0 and 1");

    return number.value();
}

Result<WindowedLayout> windowsFromValue(const Value& value)
{
    WindowedLayout layout{};

    auto bar = stringsAt(value, "bar", false);
    if (!bar)
        return bar.error();
    layout.bar = std::move(bar).value();

    const auto* pages = value.find("pages");
    const auto* items = pages != nullptr ? pages->asArray() : nullptr;
    if (items == nullptr || items->empty())
        return fail(ErrorCode::invalidPayload, "a windowed layout holds at least one page");

    for (const auto& item : *items)
    {
        Page page{};

        auto panel = item.stringAt("panel");
        if (!panel)
            return panel.error();
        if (panel.value().empty())
            return fail(ErrorCode::invalidPayload, "a page has a panel");
        page.panel = panel.value();

        auto title = item.stringAt("title");
        if (!title)
            return title.error();
        page.title = title.value();

        if (const auto* shortcut = item.find("shortcut"); shortcut != nullptr)
        {
            auto text = shortcut->asString();
            if (!text)
                return text.error();
            page.shortcut = text.value();
        }

        if (const auto* open = item.find("open"); open != nullptr)
        {
            auto flag = open->asBool();
            if (!flag)
                return flag.error();
            page.open = flag.value();
        }

        auto x = fractionAt(item, "x", page.x);
        auto y = fractionAt(item, "y", page.y);
        auto width = fractionAt(item, "width", page.width);
        auto height = fractionAt(item, "height", page.height);
        for (const auto* part : {&x, &y, &width, &height})
        {
            if (!*part)
                return part->error();
        }

        page.x = x.value();
        page.y = y.value();
        page.width = width.value();
        page.height = height.value();

        if (page.width <= 0.0 || page.height <= 0.0)
            return fail(ErrorCode::invalidPayload, "a page has a size: " + page.panel);

        layout.pages.push_back(std::move(page));
    }

    return layout;
}

void collectPanels(const LayoutNode& node, std::vector<std::string>& into)
{
    if (node.isLeaf())
    {
        into.push_back(node.panel);
        return;
    }

    for (const auto& child : node.children)
        collectPanels(child, into);
}

} // namespace

Result<WorkspaceManifest> WorkspaceManifest::parse(std::string_view json)
{
    auto value = domain::json::read(json);
    if (!value)
        return value.error();

    return fromValue(value.value());
}

Result<WorkspaceManifest> WorkspaceManifest::fromValue(const Value& value)
{
    auto schemaVersion = value.intAt("schemaVersion");
    if (!schemaVersion)
        return schemaVersion.error();

    // A manifest from a later version is refused, not read as far as it goes:
    // a workspace half-built is worse than a workspace that says why it is not
    // there.
    if (schemaVersion.value() != supportedSchemaVersion)
        return fail(ErrorCode::invalidPayload,
                    "workspace schema " + std::to_string(schemaVersion.value()) + " is not supported");

    auto id = value.stringAt("id");
    if (!id)
        return id.error();

    auto label = value.stringAt("label");
    if (!label)
        return label.error();

    WorkspaceManifest manifest{};
    manifest.id = id.value();
    manifest.label = label.value();

    if (const auto* description = value.find("description"); description != nullptr)
    {
        auto text = description->asString();
        if (!text)
            return text.error();
        manifest.description = text.value();
    }

    if (const auto* workshop = value.find("workshop"); workshop != nullptr)
    {
        auto flag = workshop->asBool();
        if (!flag)
            return flag.error();
        manifest.workshop = flag.value();
    }

    auto panels = stringsAt(value, "panels", true);
    if (!panels)
        return panels.error();
    manifest.panels = std::move(panels).value();

    if (manifest.panels.empty())
        return fail(ErrorCode::invalidPayload, "a workspace declares at least one panel");

    auto commands = stringsAt(value, "commands", false);
    if (!commands)
        return commands.error();
    manifest.commands = std::move(commands).value();

    const auto* layout = value.find("layout");
    if (layout == nullptr)
        return fail(ErrorCode::invalidPayload, "layout is missing");

    // A layout with pages is made of windows; anything else is a split tree.
    if (layout->find("pages") != nullptr)
    {
        auto windows = windowsFromValue(*layout);
        if (!windows)
            return windows.error();
        manifest.windows = std::move(windows).value();
    }
    else
    {
        auto root = nodeFromValue(*layout);
        if (!root)
            return root.error();
        manifest.layout = std::move(root).value();
    }

    // What the JSON schema cannot express: the layout and the declaration have
    // to agree. A layout that places an undeclared panel would build a screen
    // the manifest does not describe; a declared panel that nothing places
    // would be a panel nobody can ever see.
    const auto placed = manifest.placedPanels();

    for (std::size_t index = 0; index < placed.size(); ++index)
    {
        if (std::find(placed.begin(), placed.begin() + static_cast<std::ptrdiff_t>(index), placed[index]) !=
            placed.begin() + static_cast<std::ptrdiff_t>(index))
            return fail(ErrorCode::invalidPayload, "this panel is placed twice: " + placed[index]);
    }

    for (const auto& name : placed)
    {
        if (std::find(manifest.panels.begin(), manifest.panels.end(), name) == manifest.panels.end())
            return fail(ErrorCode::invalidPayload, "the layout places an undeclared panel: " + name);
    }

    for (const auto& name : manifest.panels)
    {
        if (std::find(placed.begin(), placed.end(), name) == placed.end())
            return fail(ErrorCode::invalidPayload, "this panel is declared but never placed: " + name);
    }

    return manifest;
}

std::vector<std::string> WorkspaceManifest::placedPanels() const
{
    std::vector<std::string> placed;

    if (windows.has_value())
    {
        placed = windows->bar;
        for (const auto& page : windows->pages)
            placed.push_back(page.panel);
        return placed;
    }

    collectPanels(layout, placed);
    return placed;
}

} // namespace daw::ui
