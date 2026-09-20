#include "daw/ui/workspace/LayoutTree.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>

namespace daw::ui
{
namespace
{

struct Collector
{
    std::vector<PanelBounds>* panels{nullptr};
    std::vector<Rect>* separators{nullptr};
    int separator{0};
};

void place(const LayoutNode& node, Rect area, Collector& into);

// Splits one extent into n parts proportional to the weights, keeping the sum
// exactly equal to the extent. The running total is what does it: each edge is
// rounded once, from the start of the surface, so the errors never add up.
std::vector<int> divide(const std::vector<double>& weights, int extent)
{
    std::vector<int> sizes;
    sizes.reserve(weights.size());

    const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
    if (total <= 0.0)
    {
        sizes.assign(weights.size(), 0);
        return sizes;
    }

    double consumed = 0.0;
    int placed = 0;

    for (std::size_t index = 0; index < weights.size(); ++index)
    {
        consumed += weights[index];

        const bool last = index + 1 == weights.size();
        const int edge =
            last ? extent : static_cast<int>(std::llround(consumed / total * static_cast<double>(extent)));

        sizes.push_back(std::max(0, edge - placed));
        placed = edge;
    }

    return sizes;
}

void placeChildren(const LayoutNode& node, Rect area, Collector& into)
{
    const auto count = node.children.size();
    const int gaps = into.separator * static_cast<int>(count - 1);

    std::vector<double> weights;
    weights.reserve(count);
    for (const auto& child : node.children)
        weights.push_back(child.weight);

    const bool vertical = node.split == LayoutNode::Split::vertical;
    const int extent = vertical ? area.height : area.width;

    // A surface that cannot even hold its separators shows nothing: zero
    // rectangles rather than negative ones, which a component would clamp
    // silently and paint over its neighbour.
    const auto sizes = divide(weights, std::max(0, extent - gaps));

    int offset = vertical ? area.y : area.x;

    for (std::size_t index = 0; index < count; ++index)
    {
        const auto size = sizes[index];

        const Rect childArea =
            vertical ? Rect{area.x, offset, area.width, size} : Rect{offset, area.y, size, area.height};

        place(node.children[index], childArea, into);
        offset += size;

        if (index + 1 < count && into.separator > 0)
        {
            if (into.separators != nullptr)
            {
                into.separators->push_back(vertical ? Rect{area.x, offset, area.width, into.separator}
                                                    : Rect{offset, area.y, into.separator, area.height});
            }
            offset += into.separator;
        }
    }
}

void place(const LayoutNode& node, Rect area, Collector& into)
{
    if (node.isLeaf())
    {
        if (into.panels != nullptr)
            into.panels->push_back(PanelBounds{node.panel, area});
        return;
    }

    placeChildren(node, area, into);
}

} // namespace

std::vector<PanelBounds> layoutPanels(const LayoutNode& root, Rect surface, LayoutOptions options)
{
    std::vector<PanelBounds> panels;
    Collector collector{&panels, nullptr, std::max(0, options.separator)};
    place(root, surface, collector);
    return panels;
}

std::vector<Rect> layoutSeparators(const LayoutNode& root, Rect surface, LayoutOptions options)
{
    std::vector<Rect> separators;
    Collector collector{nullptr, &separators, std::max(0, options.separator)};
    place(root, surface, collector);
    return separators;
}

} // namespace daw::ui
