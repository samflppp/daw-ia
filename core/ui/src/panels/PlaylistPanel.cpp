#include "daw/ui/panels/PlaylistPanel.h"

#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/ui/model/PatternEditing.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace daw::ui
{
namespace
{

constexpr int playheadRefreshMs = 33;
constexpr int beatsPerBar = 4;

// Sixteen bars on screen at least, and four bars of room after the last
// laying, so there is always somewhere to click the next one.
constexpr double minimumVisibleBeats = 64.0;
constexpr double roomAfterSongBeats = 16.0;

enum LaneMenu
{
    renameItem = 1,
    removeItem = 2
};

} // namespace

PlaylistPanel::PlaylistPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
{
    setLookAndFeel(&lookAndFeel_);

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
    startTimer(playheadRefreshMs);
}

PlaylistPanel::~PlaylistPanel()
{
    stopTimer();
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void PlaylistPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);
    repaint();
}

// --- geometry ---------------------------------------------------------------

juce::Rectangle<int> PlaylistPanel::headerArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                       tokens_.integer("metric.playlist.rulerHeight"));
    return area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
}

juce::Rectangle<int> PlaylistPanel::rulerArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    return area.removeFromTop(tokens_.integer("metric.playlist.rulerHeight"));
}

juce::Rectangle<int> PlaylistPanel::gridArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                       tokens_.integer("metric.playlist.rulerHeight"));
    area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    return area;
}

double PlaylistPanel::visibleBeats() const
{
    double end = 0.0;
    for (const auto& placement : state_.arrangement())
    {
        if (const auto* pattern = state_.findPattern(placement.patternId); pattern != nullptr)
            end = std::max(end, placement.startBeats + pattern->lengthBeats);
    }

    const auto wanted = std::max(minimumVisibleBeats, end + roomAfterSongBeats);
    return std::ceil(wanted / beatsPerBar) * beatsPerBar;
}

double PlaylistPanel::beatAtX(int x) const
{
    const auto grid = gridArea();
    if (grid.getWidth() <= 0)
        return 0.0;

    return static_cast<double>(x - grid.getX()) * visibleBeats() / static_cast<double>(grid.getWidth());
}

int PlaylistPanel::xForBeat(double beats) const
{
    const auto grid = gridArea();
    return grid.getX() + static_cast<int>(std::lround(beats * grid.getWidth() / visibleBeats()));
}

int PlaylistPanel::laneAtY(int y) const
{
    const auto grid = gridArea();
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    if (y < grid.getY() || laneHeight <= 0)
        return -1;

    const auto lane = (y - grid.getY()) / laneHeight;
    return lane < static_cast<int>(state_.patterns().size()) ? lane : -1;
}

juce::Rectangle<int> PlaylistPanel::blockBounds(const domain::Placement& placement) const
{
    const auto index = state_.patternIndex(placement.patternId);
    const auto* pattern = state_.findPattern(placement.patternId);
    if (!index || pattern == nullptr)
        return {};

    const auto grid = gridArea();
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    const auto left = xForBeat(placement.startBeats);
    const auto right = xForBeat(placement.startBeats + pattern->lengthBeats);

    return juce::Rectangle<int>{left,
                                grid.getY() + static_cast<int>(index.value()) * laneHeight,
                                std::max(1, right - left),
                                laneHeight}
        .reduced(0, tokens_.integer("metric.playlist.blockInset"));
}

const domain::Placement* PlaylistPanel::placementAt(juce::Point<int> point) const
{
    const domain::Placement* found = nullptr;
    for (const auto& placement : state_.arrangement())
    {
        if (blockBounds(placement).contains(point))
            found = &placement;
    }
    return found;
}

double PlaylistPanel::snap(double beats, bool fine)
{
    const auto step = fine ? 1.0 : static_cast<double>(beatsPerBar);
    return std::max(0.0, std::floor(beats / step) * step);
}

// --- painting ---------------------------------------------------------------

void PlaylistPanel::paint(juce::Graphics& g)
{
    g.fillAll(tokens_.colour("color.surface.sunken"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(header);
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    g.drawText("PLAYLIST", header, juce::Justification::centredLeft, false);

    if (state_.patterns().empty())
    {
        paintEmpty(g);
        return;
    }

    paintRuler(g, rulerArea());
    paintLanes(g, gridArea(), headerArea());
    paintBlocks(g, gridArea());
    paintPlayhead(g);
}

void PlaylistPanel::paintEmpty(juce::Graphics& g) const
{
    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    g.drawText(
        u8"créez un pattern dans le channel rack", getLocalBounds(), juce::Justification::centred, false);
}

void PlaylistPanel::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(area);

    g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));

    const auto bars = static_cast<int>(visibleBeats()) / beatsPerBar;

    // A number every bar while they fit, every fourth bar when they would not.
    const auto barWidth = area.getWidth() / std::max(1, bars);
    const auto every = barWidth >= tokens_.integer("space.xl") ? 1 : 4;

    for (int bar = 0; bar < bars; bar += every)
    {
        const auto x = xForBeat(static_cast<double>(bar * beatsPerBar));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.drawText(juce::String(bar + 1),
                   juce::Rectangle<int>{
                       x + tokens_.integer("space.xs"), area.getY(), barWidth * every, area.getHeight()},
                   juce::Justification::centredLeft,
                   false);
    }

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(area.getX(),
               area.getBottom() - tokens_.integer("stroke.hairline"),
               area.getWidth(),
               tokens_.integer("stroke.hairline"));
}

void PlaylistPanel::paintLanes(juce::Graphics& g,
                               juce::Rectangle<int> grid,
                               juce::Rectangle<int> headers) const
{
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    const auto hairline = tokens_.integer("stroke.hairline");
    const auto* shown = patternEditing::current(state_, selection_);

    // Bar lines first, under everything.
    const auto bars = static_cast<int>(visibleBeats()) / beatsPerBar;
    for (int bar = 0; bar <= bars; ++bar)
    {
        const auto x = xForBeat(static_cast<double>(bar * beatsPerBar));
        g.setColour(tokens_.colour(bar % 4 == 0 ? "color.grid.bar" : "color.grid.beat"));
        g.fillRect(x, grid.getY(), hairline, grid.getHeight());
    }

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(headers);

    for (std::size_t index = 0; index < state_.patterns().size(); ++index)
    {
        const auto& pattern = state_.patterns()[index];
        const auto y = grid.getY() + static_cast<int>(index) * laneHeight;
        if (y >= grid.getBottom())
            break;

        auto name = headers.withY(y).withHeight(laneHeight);

        // The pattern the rack and the piano roll show is the lit lane: one
        // choice, three screens.
        if (shown != nullptr && pattern.id == shown->id)
        {
            g.setColour(tokens_.colour("color.state.selected"));
            g.fillRect(name);
        }

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(grid.getX(), y + laneHeight - hairline, grid.getWidth(), hairline);
        g.fillRect(name.getX(), name.getBottom() - hairline, name.getWidth(), hairline);

        g.setColour(tokens_.colour("color.text.primary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
        g.drawText(patternEditing::displayName(state_, pattern),
                   name.reduced(tokens_.integer("space.sm"), 0),
                   juce::Justification::centredLeft,
                   true);
    }

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(headers.getRight() - hairline, headers.getY(), hairline, headers.getHeight());
}

void PlaylistPanel::paintBlocks(juce::Graphics& g, juce::Rectangle<int> grid) const
{
    const auto radius = tokens_.number("radius.sm");
    const auto* shown = patternEditing::current(state_, selection_);

    g.saveState();
    g.reduceClipRegion(grid);

    for (const auto& placement : state_.arrangement())
    {
        const auto block = blockBounds(placement);
        if (block.isEmpty())
            continue;

        const auto* pattern = state_.findPattern(placement.patternId);
        const auto current = shown != nullptr && pattern != nullptr && pattern->id == shown->id;

        // The layings of the pattern being edited are lit: an edit in the rack
        // lands in every one of them, and the screen says so before the ear
        // does.
        g.setColour(tokens_.colour(current ? "color.note.fill" : "color.note.fillSoft"));
        g.fillRoundedRectangle(block.toFloat(), radius);

        if (pattern != nullptr)
        {
            g.setColour(tokens_.colour("color.note.label"));
            g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.medium"));
            g.drawText(patternEditing::displayName(state_, *pattern),
                       block.reduced(tokens_.integer("space.xs"), 0),
                       juce::Justification::centredLeft,
                       true);
        }
    }

    g.restoreState();
}

std::optional<int> PlaylistPanel::playheadX() const
{
    if (state_.transport().mode != domain::PlayMode::song)
        return {};

    const auto beats = clock_.positionBeats();
    if (beats < 0.0 || beats > visibleBeats())
        return {};

    return xForBeat(beats);
}

void PlaylistPanel::paintPlayhead(juce::Graphics& g) const
{
    const auto x = playheadX();
    if (!x.has_value())
        return;

    const auto grid = gridArea();
    const auto ruler = rulerArea();
    g.setColour(tokens_.colour("color.accent.live"));
    g.fillRect(*x, ruler.getY(), tokens_.integer("stroke.playhead"), grid.getBottom() - ruler.getY());
}

void PlaylistPanel::timerCallback()
{
    // Only the two columns the playhead leaves and reaches are repainted.
    const auto wanted = playheadX();
    if (wanted == paintedPlayheadX_)
        return;

    const auto top = rulerArea().getY();
    const auto height = getHeight() - top;
    const auto width = tokens_.integer("stroke.playhead") + 2;

    if (paintedPlayheadX_.has_value())
        repaint(*paintedPlayheadX_ - 1, top, width, height);
    if (wanted.has_value())
        repaint(*wanted - 1, top, width, height);

    paintedPlayheadX_ = wanted;
}

// --- editing ----------------------------------------------------------------

void PlaylistPanel::placeAt(int lane, double beats)
{
    if (lane < 0 || lane >= static_cast<int>(state_.patterns().size()))
        return;

    const auto patternId = state_.patterns()[static_cast<std::size_t>(lane)].id;

    // The identifier is drawn here, by the caller, like every identifier in
    // this project: the command engenders none.
    static_cast<void>(bus_.execute(
        std::make_unique<domain::PlacePattern>(domain::PlacementId::generate(), patternId, beats)));
    selection_.selectPattern(patternId);
}

void PlaylistPanel::renamePattern(domain::PatternId patternId)
{
    const auto* pattern = state_.findPattern(patternId);
    if (pattern == nullptr)
        return;

    auto* window = new juce::AlertWindow(u8"Renommer le pattern", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", juce::String::fromUTF8(pattern->name.c_str()));
    window->addButton("Renommer", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [this, window, patternId](int result)
                                {
                                    if (result != 1)
                                        return;

                                    const auto name = window->getTextEditorContents("name").trim();
                                    static_cast<void>(bus_.execute(std::make_unique<domain::RenamePattern>(
                                        patternId, name.toStdString())));
                                }),
                            true);
}

void PlaylistPanel::showLaneMenu(int lane)
{
    if (lane < 0 || lane >= static_cast<int>(state_.patterns().size()))
        return;

    const auto patternId = state_.patterns()[static_cast<std::size_t>(lane)].id;

    juce::PopupMenu menu;
    menu.addItem(renameItem, u8"Renommer…");
    menu.addItem(removeItem, u8"Supprimer le pattern");

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this),
                       [this, patternId](int chosen)
                       {
                           if (chosen == renameItem)
                               renamePattern(patternId);
                           else if (chosen == removeItem)
                               static_cast<void>(
                                   bus_.execute(std::make_unique<domain::RemovePattern>(patternId)));
                       });
}

void PlaylistPanel::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getPosition();

    // The ruler moves the playhead, in song mode: pattern mode plays from the
    // pattern's own start, which is nowhere on this timeline.
    if (rulerArea().contains(point))
    {
        if (state_.transport().mode == domain::PlayMode::song)
        {
            static_cast<void>(bus_.execute(
                std::make_unique<domain::TransportSetPosition>(snap(beatAtX(point.getX()), true))));
        }
        return;
    }

    const auto lane = laneAtY(point.getY());

    if (headerArea().contains(point))
    {
        if (lane < 0)
            return;

        if (event.mods.isRightButtonDown())
        {
            showLaneMenu(lane);
            return;
        }

        // Choosing a pattern is not an edit: it goes to the Selection, and the
        // rack, the piano roll and pattern mode follow it from there.
        selection_.selectPattern(state_.patterns()[static_cast<std::size_t>(lane)].id);
        return;
    }

    if (!gridArea().contains(point))
        return;

    if (const auto* placement = placementAt(point); placement != nullptr)
    {
        if (event.mods.isRightButtonDown())
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::RemovePlacement>(placement->id)));
            return;
        }

        selection_.selectPattern(placement->patternId);

        Drag drag{};
        drag.placementId = placement->id;
        drag.gesture = bus_.beginGesture("déplacer un placement");
        drag.grabBeats = beatAtX(point.getX()) - placement->startBeats;
        drag.lastStartBeats = placement->startBeats;
        drag_ = drag;
        return;
    }

    if (event.mods.isRightButtonDown() || lane < 0)
        return;

    placeAt(lane, snap(beatAtX(point.getX()), event.mods.isShiftDown()));
}

void PlaylistPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (!drag_.has_value())
        return;

    const auto start = snap(beatAtX(event.getPosition().getX()) - drag_->grabBeats, event.mods.isShiftDown());

    // One command per new position, not per mouse move: a slow hand would
    // otherwise send hundreds of identical moves inside the gesture.
    if (start == drag_->lastStartBeats)
        return;

    domain::ExecuteOptions options{};
    options.gesture = drag_->gesture;

    if (bus_.execute(std::make_unique<domain::MovePlacement>(drag_->placementId, start), options).ok())
        drag_->lastStartBeats = start;
}

void PlaylistPanel::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (!drag_.has_value())
        return;

    static_cast<void>(bus_.endGesture(drag_->gesture));
    drag_.reset();
}

void PlaylistPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!headerArea().contains(event.getPosition()))
        return;

    const auto lane = laneAtY(event.getPosition().getY());
    if (lane >= 0)
        renamePattern(state_.patterns()[static_cast<std::size_t>(lane)].id);
}

} // namespace daw::ui
