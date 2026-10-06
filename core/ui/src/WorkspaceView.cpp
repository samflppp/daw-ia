#include "daw/ui/WorkspaceView.h"

#include "daw/domain/commands/TransportCommands.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

namespace daw::ui
{
namespace
{

// The function keys a page may be bound to, by the name the manifest uses.
[[nodiscard]] int functionKeyCode(const std::string& name)
{
    static const int codes[] = {juce::KeyPress::F1Key,
                                juce::KeyPress::F2Key,
                                juce::KeyPress::F3Key,
                                juce::KeyPress::F4Key,
                                juce::KeyPress::F5Key,
                                juce::KeyPress::F6Key,
                                juce::KeyPress::F7Key,
                                juce::KeyPress::F8Key,
                                juce::KeyPress::F9Key,
                                juce::KeyPress::F10Key,
                                juce::KeyPress::F11Key,
                                juce::KeyPress::F12Key};

    if (name.size() < 2 || name.front() != 'F')
        return 0;

    const auto number = std::atoi(name.c_str() + 1);
    if (number < 1 || number > 12)
        return 0;

    return codes[number - 1];
}

[[nodiscard]] Rect toRect(juce::Rectangle<int> area)
{
    return Rect{area.getX(), area.getY(), area.getWidth(), area.getHeight()};
}

[[nodiscard]] juce::Rectangle<int> toJuce(Rect rect)
{
    return {rect.x, rect.y, rect.width, rect.height};
}

} // namespace

WorkspaceView::WorkspaceView(const PanelServices& services, const PanelRegistry& registry)
    : services_(services)
    , registry_(registry)
{
    setLookAndFeel(&services_.lookAndFeel);
    setOpaque(true); // paint() fills the whole rectangle: what is behind is never painted

    // Without this, a click on a panel that wants no focus leaves the key
    // press with nobody to hand it to.
    setWantsKeyboardFocus(true);

    automationRequests_ = services_.selection.automationRequests();
    services_.selection.addChangeListener(this);
}

WorkspaceView::~WorkspaceView()
{
    services_.selection.removeChangeListener(this);
    pages_.clear();
    panels_.clear();
    setLookAndFeel(nullptr);
}

void WorkspaceView::show(const WorkspaceManifest& manifest)
{
    pages_.clear();
    panels_.clear();

    layout_ = manifest.layout;
    workspaceId_ = juce::String(manifest.id);

    if (manifest.windows.has_value())
    {
        for (const auto& id : manifest.windows->bar)
        {
            PanelContext context{services_, id};
            auto panel = registry_.create(context);
            addAndMakeVisible(*panel);
            panels_.push_back(Placed{juce::String(id), std::move(panel)});
        }

        buildPages(*manifest.windows);
    }
    else
    {
        // The order is the one the layout places them in, so the rectangles
        // that come back from layoutPanels() line up with this list index for
        // index.
        for (const auto& id : manifest.placedPanels())
        {
            PanelContext context{services_, id};
            auto panel = registry_.create(context);

            addAndMakeVisible(*panel);
            panels_.push_back(Placed{juce::String(id), std::move(panel)});
        }
    }

    resized();
    repaint();

    // The view takes the focus when the screen is built, so a shortcut works
    // before anything has been clicked. A panel that wants the focus takes it
    // from here on the first click, and the key press still comes back up.
    grabKeyboardFocus();
}

// --- windows ----------------------------------------------------------------

void WorkspaceView::buildPages(const WindowedLayout& layout)
{
    pages_.reserve(layout.pages.size());

    juce::String described;
    for (const auto& page : layout.pages)
        described << page.panel << ':' << page.x << ',' << page.y << ',' << page.width << ',' << page.height
                  << ';';
    layoutSignature_ = juce::String::toHexString(described.hashCode64());

    for (const auto& page : layout.pages)
    {
        PanelContext context{services_, page.panel, true};

        PageSlot slot{};
        slot.page = page;
        slot.open = page.open;
        slot.place = PageFractions{page.x, page.y, page.width, page.height};
        recall(slot);

        slot.window = std::make_unique<PageWindow>(services_.tokens,
                                                   services_.lookAndFeel,
                                                   juce::String::fromUTF8(page.title.c_str()),
                                                   registry_.create(context));

        const auto label = page.shortcut.empty() ? page.title : page.title + "  " + page.shortcut;
        slot.tab = std::make_unique<juce::TextButton>(juce::String::fromUTF8(label.c_str()));
        slot.tab->setClickingTogglesState(false);

        pages_.push_back(std::move(slot));
    }

    // Wired once the vector no longer moves: each callback finds its slot by
    // panel name, never by an address that a reallocation would invalidate.
    for (auto& slot : pages_)
    {
        const auto name = slot.page.panel;
        auto& window = *slot.window;

        window.desktop = [this] { return desktopArea(); };

        // The windows it can land on and be held to: the others open and not
        // maximised, in the order of the pages, both ways (S21).
        const auto others = [this, name]
        {
            std::vector<PageSlot*> found;
            for (auto& other : pages_)
            {
                if (other.page.panel != name && other.open && !other.maximised && other.window->isVisible())
                    found.push_back(&other);
            }
            return found;
        };
        window.neighbours = [others]
        {
            std::vector<juce::Rectangle<int>> areas;
            for (const auto* other : others())
                areas.push_back(other->window->getBounds());
            return areas;
        };
        window.placeNeighbours = [this, others](const std::vector<juce::Rectangle<int>>& areas)
        {
            const auto found = others();
            for (std::size_t index = 0; index < found.size() && index < areas.size(); ++index)
            {
                auto& other = *found[index];
                if (other.window->getBounds() == areas[index])
                    continue;
                other.window->setBounds(areas[index]);
                other.place = pageFractions(toRect(areas[index]), toRect(desktopArea()));
                remember(other);
            }
        };

        window.onClose = [this, name]
        {
            if (auto* found = slotFor(name); found != nullptr)
                setOpen(*found, false);
        };

        window.onMaximise = [this, name]
        {
            if (auto* found = slotFor(name); found != nullptr)
            {
                found->maximised = !found->maximised;
                placePage(*found);
                bringToFront(*found);
            }
        };

        window.onFront = [this, name]
        {
            if (auto* found = slotFor(name); found != nullptr)
                markActive(found);
        };

        window.onMoved = [this, name]
        {
            auto* found = slotFor(name);
            if (found == nullptr || placing_)
                return;

            // A moved window is no longer maximised: the user has put it
            // somewhere, and that somewhere is what is remembered.
            found->maximised = false;
            found->place = pageFractions(toRect(found->window->getBounds()), toRect(desktopArea()));
            remember(*found);
        };

        slot.tab->onClick = [this, name]
        {
            if (auto* found = slotFor(name); found != nullptr)
            {
                if (found->open && found->window->isActive())
                    setOpen(*found, false);
                else
                    setOpen(*found, true);
            }
        };

        addAndMakeVisible(*slot.tab);
        addChildComponent(*slot.window);
        slot.window->setVisible(slot.open);
        slot.tab->setToggleState(slot.open, juce::dontSendNotification);
    }

    // The last open page in the manifest's order starts in front.
    for (auto slot = pages_.rbegin(); slot != pages_.rend(); ++slot)
    {
        if (slot->open)
        {
            markActive(&*slot);
            break;
        }
    }
}

WorkspaceView::PageSlot* WorkspaceView::slotFor(std::string_view panel)
{
    const auto found = std::find_if(
        pages_.begin(), pages_.end(), [panel](const PageSlot& slot) { return slot.page.panel == panel; });
    return found == pages_.end() ? nullptr : &(*found);
}

juce::Rectangle<int> WorkspaceView::barArea() const
{
    auto area = getLocalBounds();
    const auto height =
        services_.tokens.integer("metric.transport.height") * static_cast<int>(panels_.size());
    return area.removeFromTop(height);
}

juce::Rectangle<int> WorkspaceView::tabArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(barArea().getHeight());
    return area.removeFromTop(services_.tokens.integer("metric.page.tabHeight"));
}

juce::Rectangle<int> WorkspaceView::desktopArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(barArea().getHeight() + services_.tokens.integer("metric.page.tabHeight"));
    return area;
}

void WorkspaceView::placePage(PageSlot& slot)
{
    const auto desktop = toRect(desktopArea());

    const PageLimits limits{services_.tokens.integer("metric.page.minWidth"),
                            services_.tokens.integer("metric.page.minHeight")};

    const auto bounds =
        slot.maximised
            ? desktop
            : pageBounds(slot.place.x, slot.place.y, slot.place.width, slot.place.height, desktop, limits);

    placing_ = true;
    slot.window->setBounds(toJuce(bounds));
    placing_ = false;
}

void WorkspaceView::setOpen(PageSlot& slot, bool open)
{
    const auto appearing = open && !slot.window->isVisible() && isShowing();
    slot.open = open;
    slot.window->setVisible(open);
    if (appearing)
        slot.window->appear();
    slot.tab->setToggleState(open, juce::dontSendNotification);

    if (open)
    {
        placePage(slot);
        bringToFront(slot);
    }
    else if (slot.window->isActive())
    {
        // The focus goes back to the view, so the keys still reach it.
        slot.window->setActive(false);
        grabKeyboardFocus();
    }

    remember(slot);
}

void WorkspaceView::bringToFront(PageSlot& slot)
{
    slot.window->toFront(false);
    markActive(&slot);
}

void WorkspaceView::markActive(const PageSlot* front)
{
    for (auto& slot : pages_)
        slot.window->setActive(&slot == front);
}

void WorkspaceView::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source != &services_.selection)
        return;
    const auto requests = services_.selection.automationRequests();
    if (requests == automationRequests_)
        return;
    automationRequests_ = requests;

    // Closed or behind another page, the playlist comes to the front.
    if (auto* slot = slotFor("playlist"); slot != nullptr)
    {
        if (slot->open)
            bringToFront(*slot);
        else
            setOpen(*slot, true);
    }
}

bool WorkspaceView::showPage(std::string_view panel, bool visible)
{
    auto* slot = slotFor(panel);
    if (slot == nullptr)
        return false;

    setOpen(*slot, visible);
    return true;
}

juce::Component* WorkspaceView::panel(std::string_view id) const
{
    for (const auto& placed : panels_)
    {
        if (placed.id == juce::String(std::string{id}))
            return placed.panel.get();
    }

    for (const auto& slot : pages_)
    {
        if (slot.page.panel == id)
            return &slot.window->panel();
    }

    return nullptr;
}

// The key carries a signature of the manifest's pages: when a manifest gains a
// page or moves one, the places kept for the old layout would lay the new one
// under them, so they are dropped and the manifest's own layout is used again.
void WorkspaceView::remember(const PageSlot& slot) const
{
    if (memory_ == nullptr)
        return;

    const auto key = "page." + workspaceId_ + "." + layoutSignature_ + "." + juce::String(slot.page.panel);
    const auto value = juce::String(slot.open ? 1 : 0) + ";" + juce::String(slot.place.x, 4) + ";" +
                       juce::String(slot.place.y, 4) + ";" + juce::String(slot.place.width, 4) + ";" +
                       juce::String(slot.place.height, 4);
    memory_->setValue(key, value);
}

void WorkspaceView::recall(PageSlot& slot) const
{
    if (memory_ == nullptr)
        return;

    const auto key = "page." + workspaceId_ + "." + layoutSignature_ + "." + juce::String(slot.page.panel);
    const auto parts = juce::StringArray::fromTokens(memory_->getValue(key), ";", {});
    if (parts.size() != 5)
        return;

    // A stored place that is not a place is ignored rather than trusted: the
    // manifest's own place is always there to fall back on.
    const auto fraction = [&parts](int index) { return parts[index].getDoubleValue(); };
    if (fraction(3) <= 0.0 || fraction(4) <= 0.0)
        return;

    slot.open = parts[0].getIntValue() != 0;
    slot.place = PageFractions{fraction(1), fraction(2), fraction(3), fraction(4)};
}

// --- shared -------------------------------------------------------------------

bool WorkspaceView::keyPressed(const juce::KeyPress& key)
{
    const auto undo = juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0};
    const auto redo = juce::KeyPress{'y', juce::ModifierKeys::ctrlModifier, 0};
    const auto redoAlternative =
        juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0};

    if (key == undo)
    {
        static_cast<void>(services_.bus.undo());
        return true;
    }

    if (key == redo || key == redoAlternative)
    {
        static_cast<void>(services_.bus.redo());
        return true;
    }

    // Space starts and stops, from anywhere the key reaches: a text field that
    // takes the space keeps it, so typing a request to the copilot never
    // starts the song.
    if (key == juce::KeyPress{juce::KeyPress::spaceKey})
    {
        // The toggle decides on the engine, not on the domain: the engine is
        // what is heard. The decision is logged with both, because a Space
        // that started the song instead of stopping it is one of the shapes
        // of the S12/S13 intermittent "no effect".
        const auto enginePlaying = services_.clock.isPlaying();
        juce::Logger::writeToLog(juce::String("ui: Space -> ") + (enginePlaying ? "stop" : "play") +
                                 " (engine " + (enginePlaying ? "playing" : "stopped") + ", domain " +
                                 (services_.state.transport().playing ? "playing" : "stopped") + ")");
        if (enginePlaying)
            static_cast<void>(services_.bus.execute(std::make_unique<domain::TransportStop>()));
        else
            static_cast<void>(services_.bus.execute(std::make_unique<domain::TransportPlay>()));
        return true;
    }

    // Ctrl+T: the computer's keyboard plays the chosen track, or stops (S23),
    // the shortcut FL gives the same button.
    if (key == juce::KeyPress{'t', juce::ModifierKeys::ctrlModifier, 0})
    {
        services_.live.setKeyboardPlaying(!services_.live.keyboardPlaying());
        return true;
    }

    // A page key opens its page, brings it to the front, or closes it when it
    // is already the one in front — the way F5, F6 and F7 behave in FL.
    for (auto& slot : pages_)
    {
        const auto code = functionKeyCode(slot.page.shortcut);
        if (code == 0 || key.getKeyCode() != code || key.getModifiers().isAnyModifierKeyDown())
            continue;

        if (slot.open && slot.window->isActive())
            setOpen(slot, false);
        else if (slot.open)
            bringToFront(slot);
        else
            setOpen(slot, true);

        return true;
    }

    return false;
}

Rect WorkspaceView::surface() const
{
    return Rect{0, 0, getWidth(), getHeight()};
}

LayoutOptions WorkspaceView::options() const
{
    LayoutOptions layoutOptions{};
    layoutOptions.separator = services_.tokens.integer("stroke.hairline");
    return layoutOptions;
}

void WorkspaceView::paint(juce::Graphics& g)
{
    g.fillAll(services_.tokens.colour("color.surface.base"));

    if (windowed())
    {
        auto tabs = tabArea();
        g.setColour(services_.tokens.colour("color.surface.panel"));
        g.fillRect(tabs);
        g.setColour(services_.tokens.colour("color.border.hairline"));
        g.fillRect(tabs.removeFromBottom(services_.tokens.integer("stroke.hairline")));
        return;
    }

    g.setColour(services_.tokens.colour("color.border.hairline"));
    for (const auto& rule : layoutSeparators(layout_, surface(), options()))
        g.fillRect(rule.x, rule.y, rule.width, rule.height);
}

void WorkspaceView::resized()
{
    if (windowed())
    {
        auto bar = barArea();
        for (auto& placed : panels_)
            placed.panel->setBounds(bar.removeFromTop(services_.tokens.integer("metric.transport.height")));

        auto tabs =
            tabArea().reduced(services_.tokens.integer("space.sm"), services_.tokens.integer("space.xs"));
        for (auto& slot : pages_)
        {
            slot.tab->setBounds(tabs.removeFromLeft(services_.tokens.integer("metric.page.tabWidth")));
            tabs.removeFromLeft(services_.tokens.integer("space.xs"));
        }

        // Pages keep their place as fractions, so a larger main window gives
        // larger pages instead of a band of empty desktop.
        for (auto& slot : pages_)
            placePage(slot);

        return;
    }

    const auto placed = layoutPanels(layout_, surface(), options());

    for (std::size_t index = 0; index < panels_.size() && index < placed.size(); ++index)
    {
        const auto& bounds = placed[index].bounds;
        panels_[index].panel->setBounds(bounds.x, bounds.y, bounds.width, bounds.height);
    }
}

} // namespace daw::ui
