#include "daw/ui/model/WindowSnap.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace daw::ui::snap
{
namespace
{

// Whether two spans share more than a point.
bool overlap(int fromA, int toA, int fromB, int toB)
{
    return std::min(toA, toB) > std::max(fromA, fromB);
}

// The smallest move, within reach, that puts one of `edges` on one of
// `targets`.
std::optional<int> nearest(const std::vector<int>& edges, const std::vector<int>& targets, int reach)
{
    std::optional<int> best;
    for (const auto edge : edges)
    {
        for (const auto target : targets)
        {
            const auto delta = target - edge;
            if (std::abs(delta) <= reach && (!best.has_value() || std::abs(delta) < std::abs(*best)))
                best = delta;
        }
    }
    return best;
}

// The vertical edges a window can land on: the desktop's, and those of the
// windows beside it over some height (the reach counts, so a window brought
// to a corner finds it).
std::vector<int> columns(Box window, Box desktop, const std::vector<Box>& others, int reach)
{
    std::vector<int> found{desktop.x, desktop.right()};
    for (const auto& other : others)
    {
        if (overlap(window.y - reach, window.bottom() + reach, other.y, other.bottom()))
        {
            found.push_back(other.x);
            found.push_back(other.right());
        }
    }
    return found;
}

std::vector<int> rows(Box window, Box desktop, const std::vector<Box>& others, int reach)
{
    std::vector<int> found{desktop.y, desktop.bottom()};
    for (const auto& other : others)
    {
        if (overlap(window.x - reach, window.right() + reach, other.x, other.right()))
        {
            found.push_back(other.y);
            found.push_back(other.bottom());
        }
    }
    return found;
}

} // namespace

Box moved(Box window, Box desktop, const std::vector<Box>& others, int reach)
{
    if (const auto dx = nearest({window.x, window.right()}, columns(window, desktop, others, reach), reach))
        window.x += *dx;
    if (const auto dy = nearest({window.y, window.bottom()}, rows(window, desktop, others, reach), reach))
        window.y += *dy;
    return window;
}

Box resized(Box window, unsigned edges, Box desktop, const std::vector<Box>& others, int reach)
{
    const auto across = columns(window, desktop, others, reach);
    const auto down = rows(window, desktop, others, reach);

    if ((edges & left) != 0U)
    {
        if (const auto dx = nearest({window.x}, across, reach))
        {
            window.x += *dx;
            window.width -= *dx;
        }
    }
    if ((edges & right) != 0U)
    {
        if (const auto dx = nearest({window.right()}, across, reach))
            window.width += *dx;
    }
    if ((edges & top) != 0U)
    {
        if (const auto dy = nearest({window.y}, down, reach))
        {
            window.y += *dy;
            window.height -= *dy;
        }
    }
    if ((edges & bottom) != 0U)
    {
        if (const auto dy = nearest({window.bottom()}, down, reach))
            window.height += *dy;
    }
    return window;
}

std::vector<Link> linked(Box window, unsigned edges, const std::vector<Box>& others)
{
    std::vector<Link> links;
    for (std::size_t index = 0; index < others.size(); ++index)
    {
        const auto& other = others[index];
        const bool sideBySide = overlap(window.y, window.bottom(), other.y, other.bottom());
        const bool stacked = overlap(window.x, window.right(), other.x, other.right());

        if ((edges & right) != 0U && sideBySide && other.x == window.right())
            links.push_back({index, left});
        if ((edges & left) != 0U && sideBySide && other.right() == window.x)
            links.push_back({index, right});
        if ((edges & bottom) != 0U && stacked && other.y == window.bottom())
            links.push_back({index, top});
        if ((edges & top) != 0U && stacked && other.bottom() == window.y)
            links.push_back({index, bottom});
    }
    return links;
}

Followed follow(Box before,
                Box after,
                const std::vector<Link>& links,
                std::vector<Box> others,
                int minWidth,
                int minHeight)
{
    // How far each edge of the window moved, then held back where a linked
    // window would become too small.
    auto dRight = after.right() - before.right();
    auto dLeft = after.x - before.x;
    auto dBottom = after.bottom() - before.bottom();
    auto dTop = after.y - before.y;

    for (const auto& link : links)
    {
        const auto& other = others[link.other];
        switch (link.edge)
        {
        case left: // the other is on the right: it shrinks when the edge goes right
            dRight = std::min(dRight, other.width - minWidth);
            break;
        case right:
            dLeft = std::max(dLeft, minWidth - other.width);
            break;
        case top:
            dBottom = std::min(dBottom, other.height - minHeight);
            break;
        case bottom:
            dTop = std::max(dTop, minHeight - other.height);
            break;
        default:
            break;
        }
    }

    Followed result{before, std::move(others)};
    result.window.x = before.x + dLeft;
    result.window.width = before.right() + dRight - result.window.x;
    result.window.y = before.y + dTop;
    result.window.height = before.bottom() + dBottom - result.window.y;

    for (const auto& link : links)
    {
        auto& other = result.others[link.other];
        switch (link.edge)
        {
        case left:
            other.x += dRight;
            other.width -= dRight;
            break;
        case right:
            other.width += dLeft;
            break;
        case top:
            other.y += dBottom;
            other.height -= dBottom;
            break;
        case bottom:
            other.height += dTop;
            break;
        default:
            break;
        }
    }
    return result;
}

} // namespace daw::ui::snap
