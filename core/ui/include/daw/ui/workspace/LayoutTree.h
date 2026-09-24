#pragma once

#include "daw/ui/workspace/WorkspaceManifest.h"

#include <string>
#include <vector>

namespace daw::ui
{

// Turns a layout tree and a surface into one rectangle per panel.
//
// Deliberately free of JUCE: this is the only part of the interface that has a
// right answer, so it is the only part a test can pin down. Everything above it
// is a matter of taste and is judged by looking.
//
// Hygiene rule 2 lives here. A panel receives a rectangle, it never computes
// one, and it is never told what is next to it.
struct Rect
{
    int x{0};
    int y{0};
    int width{0};
    int height{0};

    [[nodiscard]] int right() const noexcept { return x + width; }
    [[nodiscard]] int bottom() const noexcept { return y + height; }
    [[nodiscard]] bool isEmpty() const noexcept { return width <= 0 || height <= 0; }

    friend bool operator==(const Rect& lhs, const Rect& rhs) noexcept
    {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width && lhs.height == rhs.height;
    }
};

struct PanelBounds
{
    std::string panel;
    Rect bounds;
};

// Separators are cut out of the surface, not drawn on top of a panel: a rule
// painted over a panel is a rule that hides one of its pixels, and at this
// density that pixel is content.
struct LayoutOptions
{
    int separator{0};
};

// The rectangles, in the order the layout places the panels.
//
// Rounding is accumulated rather than applied per child, so a row of three
// panels covers the surface exactly instead of leaving a one-pixel gap at the
// far end. A surface too small for its separators yields empty rectangles, and
// an empty rectangle is a panel that is not shown — never a negative size.
[[nodiscard]] std::vector<PanelBounds>
layoutPanels(const LayoutNode& root, Rect surface, LayoutOptions options = {});

// The separator rules, so the layout host can paint them. They belong to the
// host, like the positions: a panel that drew its own border would be a panel
// that knows it has a neighbour.
[[nodiscard]] std::vector<Rect>
layoutSeparators(const LayoutNode& root, Rect surface, LayoutOptions options = {});

// Where a page opens on a desktop: its fractions turned into pixels, then
// brought back inside the desktop and up to the minimum size. A page that
// cannot be reached is a page that cannot be closed.
struct PageLimits
{
    int minimumWidth{0};
    int minimumHeight{0};
};

[[nodiscard]] Rect
pageBounds(double x, double y, double width, double height, Rect desktop, PageLimits limits);

// The same rectangle as fractions of the desktop: what the application stores
// when the user has moved a page, so it reopens in the same place on a screen
// of another size.
struct PageFractions
{
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};
};

[[nodiscard]] PageFractions pageFractions(Rect bounds, Rect desktop);

} // namespace daw::ui
