#include "daw/ui/model/ListScroll.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::listScroll
{

int clamped(int offset, int content, int view) noexcept
{
    return std::clamp(offset, 0, std::max(0, content - view));
}

int wheeled(int offset, double deltaY, int rowHeight, int content, int view) noexcept
{
    const auto moved = static_cast<int>(std::lround(deltaY * rowsPerWheelUnit * rowHeight));
    return clamped(offset - moved, content, view);
}

int framed(int offset, int top, int bottom, int content, int view) noexcept
{
    if (top < offset || bottom - top > view)
        return clamped(top, content, view);
    if (bottom > offset + view)
        return clamped(bottom - view, content, view);
    return clamped(offset, content, view);
}

int afterAdding(int offset, int added, int rowHeight, int content, int view) noexcept
{
    if (offset <= 0 || added <= 0)
        return clamped(offset, content, view);
    return clamped(offset + added * rowHeight, content, view);
}

} // namespace daw::ui::listScroll
