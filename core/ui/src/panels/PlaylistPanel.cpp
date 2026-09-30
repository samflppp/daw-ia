#include "daw/ui/panels/PlaylistPanel.h"

#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/ui/model/AutomationEditing.h"
#include "daw/ui/model/LaneEditing.h"
#include "daw/ui/model/PatternEditing.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace daw::ui
{
namespace
{

// Sixteen bars on screen at least, and four bars of room after the last
// laying, so there is always somewhere to click the next one.
constexpr double minimumVisibleBeats = 64.0;
constexpr double roomAfterSongBeats = 16.0;

// A notch of the wheel is about a quarter of a unit in JUCE. Zoom doubles over
// four notches; a scroll moves an eighth of the view per notch, the lanes two
// lanes' worth.
constexpr double zoomPerWheelUnit = 2.0;
constexpr double viewPerWheelUnit = 0.5;
constexpr double lanesPerWheelUnit = 8.0;

// How far right the view may go: 999 bars of 4/4. The timeline itself ends
// four bars after the song, but a person lays the next thing beyond it, the
// way FL's playlist lets them: scrolling past the end extends the view.
constexpr double longestViewBeats = 999.0 * 4.0;

enum LaneMenu
{
    renameItem = 1,
    removeItem = 2,
    renameLineItem = 3,
    insertLineItem = 4,
    removeLineItem = 5
};

// A sample the browser hands over, by its drag description.
constexpr const char* samplePrefix = "sample:";

[[nodiscard]] std::optional<juce::File> sampleFrom(const juce::DragAndDropTarget::SourceDetails& details)
{
    const auto description = details.description.toString();
    if (!description.startsWith(samplePrefix))
        return std::nullopt;

    return juce::File{description.fromFirstOccurrenceOf(samplePrefix, false, false)};
}

[[nodiscard]] double ceilToBar(double beats, double bar)
{
    return std::ceil(beats / bar - 1e-9) * bar;
}

} // namespace

PlaylistPanel::PlaylistPanel(const PanelContext& context, bool canvas)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
    , samples_(context.samples)
    , reader_(context.prompts)
    , listening_(context.listening)
    , bar_(context.tokens, context.lookAndFeel)
    , canvas_(canvas)
{
    titled_ = context.titled;

    bar_.onKey = [this](const juce::KeyPress& key) { return zoneKey(key); };
    bar_.onVariant = [this](int delta) { showZoneVariant(delta); };
    bar_.onAccept = [this] { acceptZone(); };
    bar_.onListen = [this] { toggleZoneListening(); };
    bar_.onClose = [this] { closeZone(); };
    bar_.onHeightChanged = [this] { resized(); };
    addChildComponent(bar_);
    setLookAndFeel(&lookAndFeel_);
    setOpaque(true); // paint() fills the whole rectangle: what is behind is never painted

    // The playlist takes the keys it knows — Ctrl+C, Ctrl+V, Ctrl+B, Delete —
    // and lets the others go up to the view: Space and Ctrl+Z still work
    // after a click here.
    setWantsKeyboardFocus(true);

    for (auto* bar : {&horizontal_, &vertical_})
    {
        bar->setAutoHide(false);
        bar->addListener(this);
        addAndMakeVisible(*bar);
    }

    project_.addChangeListener(this);
    selection_.addChangeListener(this);
    samples_.addChangeListener(this);
    static_cast<void>(previews_.refresh(state_));
    if (canvas_)
        static_cast<void>(bands_.refresh(state_));
    tempoLaneShown_ = tempoLaneHeight() > 0;
}

PlaylistPanel::~PlaylistPanel()
{
    stopZoneListening();
    reader_.cancel();
    horizontal_.removeListener(this);
    vertical_.removeListener(this);
    samples_.removeChangeListener(this);
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void PlaylistPanel::resized()
{
    placeBar();
    const auto thickness = tokens_.integer("metric.scrollbar.thickness");
    auto area = bodyArea();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));

    auto bottom = area.removeFromBottom(thickness);
    bottom.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    bottom.removeFromRight(thickness);
    horizontal_.setBounds(bottom);

    area.removeFromTop(tokens_.integer("metric.playlist.rulerHeight") + tempoLaneHeight());
    vertical_.setBounds(area.removeFromRight(thickness));

    updateScrollBars();
}

void PlaylistPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // A waveform came back from its thread: draw it, nothing else changed.
    if (source == &samples_)
    {
        repaint();
        return;
    }

    // The project changed: the previews whose notes changed are rebuilt, the
    // others are kept. Here, and never at paint time.
    const auto notesOnly = source == &project_ && project_.onlyNotesSince(handledRevision_);
    if (source == &project_)
    {
        handledRevision_ = project_.revision();
        static_cast<void>(previews_.refresh(state_));
        if (canvas_)
            contentChanged();
    }

    // On the canvas a note can make its line taller or shorter, and every
    // line under it moves: the whole panel is repainted.
    auto reshaped = canvas_ && source == &project_;

    // A slider asked to see its automation line: its lane comes into view.
    if (source == &selection_ && selection_.automationRequests() != automationRequests_)
    {
        automationRequests_ = selection_.automationRequests();
        revealAutomation(selection_.automationLine());
    }

    // A proposal whose zone changed under it answers another question.
    if (source == &project_ && zoneProposal_.has_value() && zoneProposal_->stale(state_))
    {
        closeZone();
        bar_.setVisible(true);
        bar_.showMessage(
            juce::String::fromUTF8(u8"Le projet a changé sous la zone : écris de nouveau ta demande."));
        resized();
        reshaped = true;
    }

    // A block an undo took away is no longer selectable.
    const auto all = items();
    selected_.erase(std::remove_if(selected_.begin(),
                                   selected_.end(),
                                   [&all](const Item& item)
                                   { return std::find(all.begin(), all.end(), item) == all.end(); }),
                    selected_.end());

    // The tempo lane comes with the first change past the origin and leaves
    // with the last: the lanes under it move, and so does the scroll bar.
    const auto automated = tempoLaneHeight() > 0;
    if (automated != tempoLaneShown_)
    {
        tempoLaneShown_ = automated;
        resized();
        reshaped = true;
    }

    // A song that grew or shrank moves the bounds of the view: an undo that
    // takes the last bars away must not leave the view past the end.
    updateScrollBars();

    // Only notes changed — a note dragged in the piano roll, a step of the
    // rack: what changed here is the picture of their pattern, in each block
    // that lays it. Those blocks are repainted, not the playlist (S18 bis).
    if (notesOnly && !reshaped && !move_.has_value())
    {
        const auto& rebuilt = previews_.rebuilt();
        const auto outline = tokens_.integer("stroke.hairline") * 2;
        for (const auto& block : content().blocks)
        {
            if (!block.clip.has_value() &&
                std::find(rebuilt.begin(), rebuilt.end(), block.pattern) != rebuilt.end())
                repaint(blockBounds(block, 0.0, 0).expanded(outline).getIntersection(gridArea()));
        }
        return;
    }

    // A step of a point being dragged changed that point's line and nothing
    // else on this screen: its lane is repainted, not the playlist (S18 bis).
    // The segments on either side of the point move with it, so the lane is
    // the smallest honest rectangle.
    if (source == &project_ && automationDrag_.has_value())
    {
        if (const auto lane = laneOfAutomation(automationDrag_->line); lane.has_value())
        {
            repaint(laneArea(*lane).getIntersection(gridArea()));
            return;
        }
    }

    repaint();
}

// --- lanes ------------------------------------------------------------------

int PlaylistPanel::freeLaneCount() const
{
    return static_cast<int>(state_.lanes().size());
}

const std::vector<PlaylistPanel::Lane>& PlaylistPanel::lanes() const
{
    return content().lanes;
}

const PlaylistPanel::Content& PlaylistPanel::content() const
{
    if (content_.built && content_.revision == project_.revision())
        return content_;

    Content fresh;
    fresh.revision = project_.revision();
    fresh.built = true;

    auto& all = fresh.lanes;
    for (const auto& lane : state_.lanes())
        all.push_back(Lane{Lane::Kind::line, lane.id, {}, {}});

    // One more, empty: laying or dropping something there makes a line.
    all.push_back(Lane{Lane::Kind::fresh, {}, {}, {}});

    // Strip by strip, in the order automationEditing::ordered gives them.
    const auto lines = automationEditing::ordered(state_);
    const auto addLinesOf = [&](domain::TrackId strip)
    {
        for (const auto* line : lines)
        {
            if (automationEditing::stripOf(state_, *line) == strip)
                all.push_back(Lane{Lane::Kind::automation, {}, strip, line->id});
        }
    };

    for (const auto& track : state_.tracks())
        addLinesOf(track.id);
    for (const auto& bus : state_.buses())
        addLinesOf(bus.id);
    addLinesOf(domain::ProjectState::masterTrackId());

    // What each lane is called, once.
    for (std::size_t lane = 0; lane < all.size(); ++lane)
    {
        const auto& entry = all[lane];
        if (entry.kind == Lane::Kind::line)
            fresh.laneLabels.push_back(laneLabel(state_.lanes()[lane], static_cast<int>(lane)));
        else if (entry.kind == Lane::Kind::fresh)
            fresh.laneLabels.push_back(juce::String::fromUTF8(u8"+ Nouvelle ligne"));
        else if (const auto* line = state_.findAutomationLine(entry.line); line != nullptr)
            fresh.laneLabels.push_back(
                juce::String::fromUTF8(automationEditing::label(state_, *line).c_str()));
        else
            fresh.laneLabels.emplace_back();
    }

    // The blocks, in the order they are drawn: layings, then samples.
    const auto laneIndex = [this](domain::LaneId id)
    {
        const auto index = state_.laneIndex(id);
        return index ? static_cast<int>(index.value()) : -1;
    };
    for (const auto& placement : state_.arrangement())
    {
        Block block{};
        block.item = Item{false, placement.id.toString()};
        block.start = placement.startBeats;
        block.lane = laneIndex(placement.laneId);
        block.pattern = placement.patternId;
        if (const auto* pattern = state_.findPattern(placement.patternId); pattern != nullptr)
        {
            block.length = pattern->lengthBeats;
            block.label = juce::String::fromUTF8(patternEditing::displayName(state_, *pattern).c_str());
        }
        fresh.blocks.push_back(std::move(block));
    }
    for (const auto& clip : state_.audioClips())
    {
        Block block{};
        block.item = Item{true, clip.id.toString()};
        block.start = clip.startBeats;
        block.lane = laneIndex(clip.laneId);
        block.clip = clip.id;
        block.length = lengthOf(block.item);
        block.label = juce::String::fromUTF8(clip.sample.name.c_str());
        fresh.blocks.push_back(std::move(block));
    }

    for (const auto& block : fresh.blocks)
        fresh.end = std::max(fresh.end, block.start + block.length);

    content_ = std::move(fresh);
    ++contentBuilds_;
    return content_;
}

int PlaylistPanel::laneCount() const
{
    return static_cast<int>(lanes().size());
}

juce::String PlaylistPanel::laneLabel(const domain::Lane& lane, int rank) const
{
    if (!lane.name.empty())
        return juce::String::fromUTF8(lane.name.c_str());

    // Unnamed, a line says what it was made for, as the S16 playlist did: the
    // pattern it came with, or the track whose audio it held.
    for (const auto& pattern : state_.patterns())
    {
        if (domain::ProjectState::laneOfPattern(pattern.id) == lane.id)
            return juce::String::fromUTF8(patternEditing::displayName(state_, pattern).c_str());
    }
    for (const auto& track : state_.tracks())
    {
        if (domain::ProjectState::laneOfTrack(track.id) == lane.id)
            return juce::String::fromUTF8(track.name.c_str());
    }
    return juce::String::fromUTF8(u8"Ligne ") + juce::String{rank + 1};
}

void PlaylistPanel::placeBar()
{
    // The generation window, docked under the playlist: the grid shrinks, it
    // is never covered.
    if (bar_.isVisible())
        bar_.setBounds(getLocalBounds().removeFromBottom(bar_.preferredHeight()));
}

juce::Rectangle<int> PlaylistPanel::bodyArea() const
{
    auto area = getLocalBounds();
    if (bar_.isVisible())
        area.removeFromBottom(bar_.preferredHeight());
    return area;
}

juce::Rectangle<int> PlaylistPanel::headerArea() const
{
    auto area = bodyArea();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                       tokens_.integer("metric.playlist.rulerHeight") + tempoLaneHeight());
    area.removeFromBottom(tokens_.integer("metric.scrollbar.thickness"));
    return area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
}

juce::Rectangle<int> PlaylistPanel::rulerArea() const
{
    auto area = bodyArea();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    area.removeFromRight(tokens_.integer("metric.scrollbar.thickness"));
    return area.removeFromTop(tokens_.integer("metric.playlist.rulerHeight"));
}

juce::Rectangle<int> PlaylistPanel::gridArea() const
{
    auto area = bodyArea();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                       tokens_.integer("metric.playlist.rulerHeight") + tempoLaneHeight());
    area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    area.removeFromRight(tokens_.integer("metric.scrollbar.thickness"));
    area.removeFromBottom(tokens_.integer("metric.scrollbar.thickness"));
    return area;
}

double PlaylistPanel::timelineBeats() const
{
    const auto wanted = std::max(minimumVisibleBeats, content().end + roomAfterSongBeats);
    return std::ceil(wanted / barBeats()) * barBeats();
}

double PlaylistPanel::barBeats() const
{
    return state_.beatsPerBar();
}

double PlaylistPanel::fitBeatWidth() const
{
    const auto width = static_cast<double>(std::max(1, gridArea().getWidth()));
    const auto floor = static_cast<double>(tokens_.integer("metric.playlist.beatWidthMin"));
    return std::max(floor, width / timelineBeats());
}

double PlaylistPanel::widestBeatWidth() const
{
    return std::max(fitBeatWidth(),
                    static_cast<double>(tokens_.integer(canvas_ ? "metric.canvas.beatWidthMax"
                                                                : "metric.playlist.beatWidthMax")));
}

double PlaylistPanel::beatWidth() const
{
    const auto fit = fitBeatWidth();
    return std::clamp(zoom_.value_or(fit), fit, widestBeatWidth());
}

double PlaylistPanel::viewBeats() const
{
    return static_cast<double>(std::max(1, gridArea().getWidth())) / beatWidth();
}

double PlaylistPanel::firstBeat() const
{
    // Past the end of the song is allowed, up to the longest view: a view
    // left there by an undo stays where the hand left it, as in FL.
    return std::clamp(firstBeat_, 0.0, std::max(0.0, longestViewBeats - viewBeats()));
}

double PlaylistPanel::beatAtX(int x) const
{
    return firstBeat() + static_cast<double>(x - gridArea().getX()) / beatWidth();
}

int PlaylistPanel::xForBeat(double beats) const
{
    return gridArea().getX() + static_cast<int>(std::lround((beats - firstBeat()) * beatWidth()));
}

int PlaylistPanel::lanesHeight() const
{
    return laneTop(laneCount());
}

int PlaylistPanel::laneTop(int lane) const
{
    if (!canvas_)
        return lane * tokens_.integer("metric.playlist.laneHeight");

    const auto& edges = laneEdges();
    if (lane < 0)
        return 0;
    if (static_cast<std::size_t>(lane) < edges.size())
        return edges[static_cast<std::size_t>(lane)];
    return edges.back() +
           (lane - static_cast<int>(edges.size()) + 1) * tokens_.integer("metric.playlist.laneHeight");
}

int PlaylistPanel::laneHeightOf(int lane) const
{
    if (!canvas_)
        return tokens_.integer("metric.playlist.laneHeight");
    return laneTop(lane + 1) - laneTop(lane);
}

int PlaylistPanel::laneAtContentY(int y) const
{
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    if (y < 0)
        return -1;
    if (!canvas_)
        return laneHeight > 0 ? y / laneHeight : 0;

    // The last edge at or above y: its lane. Past the last lane, as many
    // playlist lines as fit, the way the playlist counts.
    const auto& edges = laneEdges();
    const auto after = std::upper_bound(edges.begin(), edges.end(), y);
    const auto lane = static_cast<int>(after - edges.begin()) - 1;
    if (after != edges.end())
        return lane;
    return lane + (laneHeight > 0 ? (y - edges.back()) / laneHeight : 0);
}

int PlaylistPanel::firstLanePixel() const
{
    return std::clamp(firstLanePixel_, 0, std::max(0, lanesHeight() - gridArea().getHeight()));
}

juce::Point<int> PlaylistPanel::pointFor(int lane, double beats) const
{
    return {xForBeat(beats), gridArea().getY() + laneTop(lane) + laneHeightOf(lane) / 2 - firstLanePixel()};
}

int PlaylistPanel::laneAtY(int y) const
{
    const auto grid = gridArea();
    if (y < grid.getY() || y >= grid.getBottom())
        return -1;

    const auto lane = laneAtContentY(y - grid.getY() + firstLanePixel());
    return lane >= 0 && lane < laneCount() ? lane : -1;
}

void PlaylistPanel::setView(double first, std::optional<double> zoom)
{
    zoom_ = zoom;

    // Zooming out stops at the width that fits: past it there is nothing
    // more to see, and "fit" becomes the state again, so the view follows
    // the song as it grows.
    if (zoom_.has_value() && *zoom_ <= fitBeatWidth())
        zoom_.reset();

    firstBeat_ = first;
    firstBeat_ = firstBeat();
    updateScrollBars();
    repaint();
}

void PlaylistPanel::setFirstLanePixel(int pixel)
{
    firstLanePixel_ = pixel;
    firstLanePixel_ = firstLanePixel();
    updateScrollBars();
    repaint();
}

void PlaylistPanel::updateScrollBars()
{
    // The bar covers the song, and the view when it went past the song.
    horizontal_.setRangeLimits(
        0.0, std::max(timelineBeats(), firstBeat() + viewBeats()), juce::dontSendNotification);
    horizontal_.setCurrentRange(firstBeat(), viewBeats(), juce::dontSendNotification);
    horizontal_.setSingleStepSize(barBeats());

    vertical_.setRangeLimits(0.0,
                             static_cast<double>(std::max(lanesHeight(), gridArea().getHeight())),
                             juce::dontSendNotification);
    vertical_.setCurrentRange(static_cast<double>(firstLanePixel()),
                              static_cast<double>(gridArea().getHeight()),
                              juce::dontSendNotification);
    vertical_.setSingleStepSize(static_cast<double>(tokens_.integer("metric.playlist.laneHeight")));
}

void PlaylistPanel::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    // The hand takes the view: what was gliding stops where it is.
    pageTurn_.reset();
    zoomGlide_.reset();

    if (bar == &horizontal_)
        setView(newRangeStart, zoom_);
    else if (bar == &vertical_)
        setFirstLanePixel(static_cast<int>(std::lround(newRangeStart)));
}

void PlaylistPanel::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (tempoWheel(event, wheel) || automationWheel(event, wheel))
        return;

    const auto grid = gridArea();

    // Over the ruler the wheel zooms, as in FL; elsewhere Ctrl makes it zoom.
    if (rulerArea().contains(event.getPosition()) || event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        zoomAround(event.getPosition(), std::pow(zoomPerWheelUnit, static_cast<double>(wheel.deltaY)), true);
        return;
    }

    // Scrolling takes the view from any glide.
    pageTurn_.reset();
    zoomGlide_.reset();

    // Sideways: a trackpad's own horizontal swipe, or Shift and the wheel.
    const auto sideways =
        wheel.deltaX != 0.0f ? wheel.deltaX : (event.mods.isShiftDown() ? wheel.deltaY : 0.0f);
    if (sideways != 0.0f)
    {
        setView(firstBeat() - static_cast<double>(sideways) * viewPerWheelUnit * viewBeats(), zoom_);
        return;
    }

    const auto laneHeight = static_cast<double>(tokens_.integer("metric.playlist.laneHeight"));
    setFirstLanePixel(firstLanePixel() - static_cast<int>(std::lround(static_cast<double>(wheel.deltaY) *
                                                                      lanesPerWheelUnit * laneHeight)));
}

void PlaylistPanel::zoomAround(juce::Point<int> anchorPoint, double factor, bool glides)
{
    // Around the pointer: the beat under it stays under it, and on the
    // canvas, where the lines grow with the zoom, the row under it too.
    const auto grid = gridArea();
    const auto x = std::clamp(anchorPoint.getX(), grid.getX(), grid.getRight());
    const auto y = std::clamp(anchorPoint.getY(), grid.getY(), grid.getBottom());
    const auto anchor = beatAtX(x);

    const auto contentY = y - grid.getY() + firstLanePixel();
    const auto lane = laneAtContentY(contentY);
    const auto height = std::max(1, laneHeightOf(lane));
    const auto within = static_cast<double>(contentY - laneTop(lane)) / static_cast<double>(height);
    pageTurn_.reset();

    // On the fluid pace the zoom glides to where the notches say, from
    // where it is; another notch aims further from the same place.
    if (glides && FrameTicker::animates())
    {
        const auto aimed = zoomGlide_.has_value() ? zoomGlide_->width.to : beatWidth();
        const auto target = std::clamp(aimed * factor, fitBeatWidth(), widestBeatWidth());
        zoomGlide_ = ZoomGlide{Glide{beatWidth(),
                                     target,
                                     FrameTicker::nowMs(),
                                     static_cast<double>(tokens_.integer("motion.duration.zoom"))},
                               anchor,
                               x,
                               lane,
                               within,
                               y};
        return;
    }

    const auto clamped = std::clamp(beatWidth() * factor, fitBeatWidth(), widestBeatWidth());
    setView(anchor - static_cast<double>(x - grid.getX()) / clamped, clamped);
    keepRowUnder(lane, within, y);
}

void PlaylistPanel::keepRowUnder(int lane, double within, int y)
{
    if (!canvas_ || lane < 0)
        return;
    const auto top = laneTop(lane) + static_cast<int>(std::lround(within * laneHeightOf(lane)));
    setFirstLanePixel(top - (y - gridArea().getY()));
}

void PlaylistPanel::followPlayhead()
{
    // Only while the song plays, only when the song does not fit, and never
    // under a hand that is moving blocks or drawing a band.
    const auto fits = fitBeatWidth() * timelineBeats() <= static_cast<double>(gridArea().getWidth()) + 0.5;
    if (!clock_.isPlaying() || state_.transport().mode != domain::PlayMode::song || move_.has_value() ||
        band_.has_value() || (!zoom_.has_value() && fits))
        return;

    const auto beats = clock_.positionBeats();
    const auto first = firstBeat();
    if (beats >= first && beats < first + viewBeats())
        return;

    // A page turn, the way FL turns it: the playhead goes back to the left
    // edge, on a bar line. On the fluid pace the view slides there, slowing
    // as it arrives, while the playhead goes on where it really is.
    const auto page = std::floor(beats / barBeats()) * barBeats();
    if (FrameTicker::animates())
    {
        if (!pageTurn_.has_value())
            pageTurn_ = Glide{first,
                              page,
                              FrameTicker::nowMs(),
                              static_cast<double>(tokens_.integer("motion.duration.page"))};
        return;
    }
    setView(page, zoom_);
}

void PlaylistPanel::glide()
{
    const auto now = FrameTicker::nowMs();
    if (pageTurn_.has_value())
    {
        setView(pageTurn_->at(now), zoom_);
        if (pageTurn_->done(now))
            pageTurn_.reset();
    }

    // The zoom, around the beat under the pointer when the wheel turned.
    if (zoomGlide_.has_value())
    {
        const auto width = zoomGlide_->width.at(now);
        setView(zoomGlide_->anchorBeats -
                    static_cast<double>(zoomGlide_->anchorX - gridArea().getX()) / width,
                width);
        keepRowUnder(zoomGlide_->anchorLane, zoomGlide_->anchorWithin, zoomGlide_->anchorY);
        if (zoomGlide_->width.done(now))
            zoomGlide_.reset();
    }
}

double PlaylistPanel::snap(double beats, bool fine) const
{
    const auto step = fine ? 1.0 : barBeats();
    return std::max(0.0, std::floor(beats / step) * step);
}

// --- items ------------------------------------------------------------------

std::vector<PlaylistPanel::Item> PlaylistPanel::items() const
{
    std::vector<Item> all;
    for (const auto& block : content().blocks)
        all.push_back(block.item);
    return all;
}

std::optional<double> PlaylistPanel::startOf(const Item& item) const
{
    if (item.audio)
    {
        const auto id = domain::AudioClipId::parse(item.id);
        const auto* clip = id ? state_.findAudioClip(id.value()) : nullptr;
        return clip != nullptr ? std::optional<double>{clip->startBeats} : std::nullopt;
    }

    const auto id = domain::PlacementId::parse(item.id);
    const auto* placement = id ? state_.findPlacement(id.value()) : nullptr;
    return placement != nullptr ? std::optional<double>{placement->startBeats} : std::nullopt;
}

double PlaylistPanel::lengthOf(const Item& item) const
{
    if (item.audio)
    {
        const auto id = domain::AudioClipId::parse(item.id);
        const auto* clip = id ? state_.findAudioClip(id.value()) : nullptr;
        if (clip == nullptr)
            return 0.0;

        // A recording lasts seconds, and the grid is in beats: read through
        // the tempo where it starts. Drawing only — the Edit uses the real
        // sequence.
        return clip->sample.seconds * state_.tempoAt(clip->startBeats) / 60.0;
    }

    const auto id = domain::PlacementId::parse(item.id);
    const auto* placement = id ? state_.findPlacement(id.value()) : nullptr;
    const auto* pattern = placement != nullptr ? state_.findPattern(placement->patternId) : nullptr;
    return pattern != nullptr ? pattern->lengthBeats : 0.0;
}

int PlaylistPanel::laneOf(const Item& item) const
{
    domain::LaneId laneId{};
    if (item.audio)
    {
        const auto id = domain::AudioClipId::parse(item.id);
        const auto* clip = id ? state_.findAudioClip(id.value()) : nullptr;
        if (clip == nullptr)
            return -1;
        laneId = clip->laneId;
    }
    else
    {
        const auto id = domain::PlacementId::parse(item.id);
        const auto* placement = id ? state_.findPlacement(id.value()) : nullptr;
        if (placement == nullptr)
            return -1;
        laneId = placement->laneId;
    }

    const auto index = state_.laneIndex(laneId);
    return index ? static_cast<int>(index.value()) : -1;
}

juce::Rectangle<int> PlaylistPanel::bounds(const Item& item, double offsetBeats, int laneOffset) const
{
    for (const auto& block : content().blocks)
    {
        if (block.item == item)
            return blockBounds(block, offsetBeats, laneOffset);
    }
    return {};
}

juce::Rectangle<int> PlaylistPanel::blockBounds(const Block& block, double offsetBeats, int laneOffset) const
{
    if (block.lane < 0)
        return {};

    // A block being dragged is drawn on the line it would land on, the empty
    // one under the last included.
    const auto lane = std::clamp(block.lane + laneOffset, 0, freeLaneCount());

    const auto grid = gridArea();
    const auto left = xForBeat(std::max(0.0, block.start + offsetBeats));
    const auto right = xForBeat(std::max(0.0, block.start + offsetBeats) + block.length);

    return juce::Rectangle<int>{
        left, grid.getY() + laneTop(lane) - firstLanePixel(), std::max(1, right - left), laneHeightOf(lane)}
        .withTrimmedTop(tokens_.integer("metric.playlist.blockInset"))
        .withTrimmedBottom(tokens_.integer("metric.playlist.blockInset"));
}

std::optional<PlaylistPanel::Item> PlaylistPanel::itemAt(juce::Point<int> point) const
{
    // The last one drawn wins, which is the one on top.
    std::optional<Item> found;
    for (const auto& block : content().blocks)
    {
        if (blockBounds(block, 0.0, 0).contains(point))
            found = block.item;
    }
    return found;
}

bool PlaylistPanel::isSelected(const Item& item) const
{
    return std::find(selected_.begin(), selected_.end(), item) != selected_.end();
}

// --- painting ---------------------------------------------------------------

void PlaylistPanel::paint(juce::Graphics& g)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    g.fillAll(tokens_.colour("color.surface.sunken"));

    auto header = getLocalBounds().removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(header);
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(header.removeFromBottom(tokens_.integer("stroke.hairline")));

    header.removeFromLeft(tokens_.integer("space.md"));
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().caps("font.size.micro"));
    if (!titled_)
        g.drawText(canvas_ ? "TOILE" : "PLAYLIST", header, juce::Justification::centredLeft, false);

    // Nothing but the line that makes a line: nothing to arrange yet.
    if (state_.lanes().empty() && laneCount() == 1)
    {
        paintEmpty(g);
        return;
    }

    paintRuler(g, rulerArea());
    paintTempoLane(g);
    paintLanes(g, gridArea(), headerArea());
    paintBlocks(g, gridArea());
    paintZone(g, gridArea());
    paintAutomation(g, gridArea());
    paintPlayhead(g);

    if (band_.has_value())
    {
        g.setColour(tokens_.colour("color.state.selected"));
        g.fillRect(*band_);
        g.setColour(tokens_.colour("color.accent.primary"));
        g.drawRect(*band_, tokens_.integer("stroke.hairline"));
    }

    // Only a repaint of the whole panel says what drawing the song costs; a
    // playhead column says nothing.
    if (g.getClipBounds().getWidth() >= getWidth() / 2)
        lastPaintMs_ = juce::Time::getMillisecondCounterHiRes() - started;
}

void PlaylistPanel::paintEmpty(juce::Graphics& g) const
{
    g.setColour(tokens_.colour("color.text.disabled"));
    g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.regular"));
    g.drawText(u8"créez un pattern dans le channel rack, ou déposez un sample ici",
               getLocalBounds(),
               juce::Justification::centred,
               false);
}

void PlaylistPanel::paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const
{
    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(area);

    g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));

    g.saveState();
    g.reduceClipRegion(area);

    // A number every bar while they fit, every fourth bar, every sixteenth.
    const auto bar = barBeats();
    const auto barWidth = static_cast<int>(beatWidth() * bar);
    const auto roomy = tokens_.integer("space.xl");
    const auto every = barWidth >= roomy ? 1 : (barWidth * 4 >= roomy ? 4 : 16);

    const auto firstBar = static_cast<int>(firstBeat() / bar) / every * every;
    const auto lastBar = static_cast<int>(std::ceil((firstBeat() + viewBeats()) / bar));

    for (int number = firstBar; number <= lastBar; number += every)
    {
        const auto x = xForBeat(number * bar);
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.drawText(juce::String(number + 1),
                   juce::Rectangle<int>{
                       x + tokens_.integer("space.xs"), area.getY(), barWidth * every, area.getHeight()},
                   juce::Justification::centredLeft,
                   false);
    }

    g.restoreState();

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
    const auto hairline = tokens_.integer("stroke.hairline");
    const auto* shown = patternEditing::current(state_, selection_);

    g.saveState();
    g.reduceClipRegion(grid.getUnion(headers));

    // Bar lines first, under everything, and only the ones in sight.
    const auto barLength = barBeats();
    const auto firstBar = static_cast<int>(firstBeat() / barLength);
    const auto lastBar = static_cast<int>(std::ceil((firstBeat() + viewBeats()) / barLength));
    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        const auto x = xForBeat(bar * barLength);
        g.setColour(tokens_.colour(bar % 4 == 0 ? "color.grid.bar" : "color.grid.beat"));
        g.fillRect(x, grid.getY(), hairline, grid.getHeight());
    }

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(headers);

    const auto& shownContent = content();
    const auto& all = shownContent.lanes;
    const auto clip = g.getClipBounds();
    for (int lane = 0; lane < static_cast<int>(all.size()); ++lane)
    {
        const auto y = grid.getY() + laneTop(lane) - firstLanePixel();
        const auto laneHeight = laneHeightOf(lane);
        if (y + laneHeight <= std::max(grid.getY(), clip.getY()))
            continue;
        if (y >= std::min(grid.getBottom(), clip.getBottom()))
            break;

        auto name = headers.withY(y).withHeight(laneHeight);
        auto title = name;
        const auto& label = shownContent.laneLabels[static_cast<std::size_t>(lane)];
        const auto& entry = all[static_cast<std::size_t>(lane)];

        if (entry.kind == Lane::Kind::line)
        {
            // The line the pattern being edited was made for is lit: one
            // choice, three screens, as before lines were free.
            if (shown != nullptr && domain::ProjectState::laneOfPattern(shown->id) == entry.id)
            {
                g.setColour(tokens_.colour("color.state.selected"));
                g.fillRect(name);
            }
        }
        else if (entry.kind == Lane::Kind::automation)
        {
            // The line a slider's right-click asked for is the lit one.
            if (entry.line == shownAutomation_)
            {
                g.setColour(tokens_.colour("color.state.selected"));
                g.fillRect(name);
            }
        }

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(grid.getX(), y + laneHeight - hairline, grid.getWidth(), hairline);
        g.fillRect(name.getX(), name.getBottom() - hairline, name.getWidth(), hairline);

        // On the canvas, the name sits over its band names.
        if (canvas_ && entry.kind == Lane::Kind::line && !bandsOfLane(lane).empty())
        {
            title = name.removeFromTop(canvasChrome());
            paintBandNames(g, lane, headers.withY(y).withHeight(laneHeight));
        }

        g.setColour(tokens_.colour(lane < freeLaneCount() ? "color.text.primary" : "color.text.secondary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
        g.drawText(
            label, title.reduced(tokens_.integer("space.sm"), 0), juce::Justification::centredLeft, true);
    }

    g.restoreState();

    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(headers.getRight() - hairline, headers.getY(), hairline, headers.getHeight());
}

void PlaylistPanel::paintBlocks(juce::Graphics& g, juce::Rectangle<int> grid) const
{
    const auto radius = tokens_.number("radius.sm");
    const auto* shown = patternEditing::current(state_, selection_);

    g.saveState();
    g.reduceClipRegion(grid);

    // Only what the repaint asked for: a window moved over the playlist
    // uncovers a strip, and the blocks outside it are not visited twice.
    const auto visible = g.getClipBounds();

    for (const auto& entry : content().blocks)
    {
        const auto& item = entry.item;
        const auto moving = move_.has_value() && isSelected(item);
        const auto block =
            blockBounds(entry, moving ? move_->offsetBeats : 0.0, moving ? move_->laneOffset : 0);
        if (block.isEmpty() || !block.intersects(visible))
            continue;

        // The layings of the pattern being edited are lit: an edit in the rack
        // lands in every one of them, and the screen says so before the ear
        // does.
        const auto lit = !item.audio && shown != nullptr && entry.pattern == shown->id;

        const auto* fill =
            item.audio ? "color.actor.copilot" : (lit ? "color.note.fill" : "color.note.fillSoft");
        g.setColour(tokens_.colour(fill));
        g.fillRoundedRectangle(block.toFloat(), radius);

        // The content, under the name band, when the block is wide enough to
        // show it as more than a smear.
        auto content = block.reduced(tokens_.integer("space.xs"), tokens_.integer("stroke.hairline"));
        content.removeFromTop(tokens_.integer("metric.playlist.labelHeight"));
        if (block.getWidth() >= tokens_.integer("metric.playlist.previewMinWidth") && !content.isEmpty())
        {
            if (entry.clip.has_value())
            {
                if (const auto* clip = state_.findAudioClip(*entry.clip); clip != nullptr)
                {
                    // Nothing yet the first time: the sample is being measured
                    // on another thread, and the block fills in when it is.
                    if (const auto peaks = samples_.waveform(clip->sample); peaks != nullptr)
                        paintWaveform(g, *peaks, block, content, grid);
                }
            }
            else if (canvas_ && !moving)
            {
                // On the canvas the block is its bands, drawn at every scale;
                // a block being moved keeps its S11 picture until it lands.
                const auto* placement = state_.findPlacement(domain::PlacementId::parse(item.id).value());
                const auto* pattern =
                    placement != nullptr ? state_.findPattern(placement->patternId) : nullptr;
                if (pattern != nullptr)
                    paintCanvasBlock(g, *placement, *pattern, block, entry.lane, grid);
            }
            else if (const auto* preview = previews_.find(entry.pattern); preview != nullptr)
            {
                paintPreview(g, *preview, content);
            }
        }

        // A selected block is outlined, so a selection of twenty reads at a
        // glance and a single one does not look like the pattern being edited.
        if (isSelected(item))
        {
            g.setColour(tokens_.colour("color.note.selected"));
            g.drawRoundedRectangle(
                block.toFloat(), radius, static_cast<float>(tokens_.integer("stroke.hairline") * 2));
        }

        g.setColour(tokens_.colour("color.note.label"));
        g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.medium"));
        g.drawText(entry.label,
                   block.reduced(tokens_.integer("space.xs"), 0)
                       .removeFromTop(tokens_.integer("metric.playlist.labelHeight") +
                                      tokens_.integer("stroke.hairline")),
                   juce::Justification::centredLeft,
                   true);
    }

    g.restoreState();
}

void PlaylistPanel::paintPreview(juce::Graphics& g,
                                 const PatternPreview& preview,
                                 juce::Rectangle<int> area) const
{
    // The geometry was computed once, when the notes changed; a repaint only
    // scales it to the block.
    const auto box = area.toFloat();
    const auto thinnest = static_cast<float>(tokens_.integer("metric.playlist.previewNoteMinHeight"));

    g.setColour(tokens_.colour("color.preview.note"));
    for (const auto& note : preview.notes)
    {
        g.fillRect(box.getX() + note.x * box.getWidth(),
                   box.getY() + note.y * box.getHeight(),
                   std::max(thinnest, note.width * box.getWidth()),
                   std::max(thinnest, note.height * box.getHeight()));
    }
}

void PlaylistPanel::paintWaveform(juce::Graphics& g,
                                  const WaveformPeaks& peaks,
                                  juce::Rectangle<int> block,
                                  juce::Rectangle<int> area,
                                  juce::Rectangle<int> visible) const
{
    // Only the columns on screen: a sample of several minutes laid under a
    // zoomed view draws the few hundred pixels that show, not the whole of it.
    const auto shown = area.getIntersection(visible);
    if (shown.isEmpty() || block.getWidth() <= 0 || peaks.seconds <= 0.0)
        return;

    const auto secondsPerPixel = peaks.seconds / static_cast<double>(block.getWidth());
    const auto from = static_cast<double>(shown.getX() - block.getX()) * secondsPerPixel;
    const auto to = static_cast<double>(shown.getRight() - block.getX()) * secondsPerPixel;
    const auto columns = peaks.columns(from, to, shown.getWidth());

    const auto middle = static_cast<float>(area.getCentreY());
    const auto half = static_cast<float>(area.getHeight()) * 0.5f;
    const auto stroke = static_cast<float>(tokens_.integer("metric.playlist.waveStroke"));

    g.setColour(tokens_.colour("color.preview.wave"));
    for (std::size_t column = 0; column < columns.size(); ++column)
    {
        const auto x = static_cast<float>(shown.getX()) + static_cast<float>(column);
        const auto top = middle - std::clamp(columns[column].maximum, -1.0f, 1.0f) * half;
        const auto bottom = middle - std::clamp(columns[column].minimum, -1.0f, 1.0f) * half;
        g.fillRect(x, top, stroke, std::max(stroke, bottom - top));
    }
}

std::optional<int> PlaylistPanel::playheadX() const
{
    if (state_.transport().mode != domain::PlayMode::song)
        return {};

    const auto beats = clock_.displayBeats();
    if (beats < firstBeat() || beats > firstBeat() + viewBeats())
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

void PlaylistPanel::frame()
{
    closeTempoWheel(true);
    closeAutomationWheel(true);
    glide();
    followPlayhead();

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
    ++playheadMoves_;
}

// --- editing ----------------------------------------------------------------

std::optional<domain::LaneId>
PlaylistPanel::lineFor(int lane, std::vector<std::unique_ptr<domain::Command>>& commands) const
{
    if (lane < 0 || lane > freeLaneCount())
        return std::nullopt;
    if (lane < freeLaneCount())
        return state_.lanes()[static_cast<std::size_t>(lane)].id;

    // The identifier is drawn here, by the caller, like every identifier in
    // this project: the command engenders none.
    const auto fresh = domain::LaneId::generate();
    commands.push_back(std::make_unique<domain::CreateLane>(fresh, std::string{}, state_.lanes().size()));
    return fresh;
}

void PlaylistPanel::placeAt(int lane, double beats)
{
    // On the line a pattern was made for, that pattern, as the S10 playlist
    // did; on any other line, the pattern being edited, as FL does.
    const auto* pattern = patternEditing::current(state_, selection_);
    if (lane >= 0 && lane < freeLaneCount())
    {
        const auto laneId = state_.lanes()[static_cast<std::size_t>(lane)].id;
        for (const auto& candidate : state_.patterns())
        {
            if (domain::ProjectState::laneOfPattern(candidate.id) == laneId)
                pattern = &candidate;
        }
    }
    if (pattern == nullptr)
        return;
    const auto patternId = pattern->id;
    selection_.selectPattern(patternId);

    std::vector<std::unique_ptr<domain::Command>> commands;
    const auto line = lineFor(lane, commands);
    if (!line.has_value())
        return;

    const auto placementId = domain::PlacementId::generate();
    commands.push_back(std::make_unique<domain::PlacePattern>(placementId, patternId, beats, *line));

    domain::GroupOptions group{};
    group.label = "poser un pattern";
    if (bus_.executeGroup(std::move(commands), group).ok())
        selected_ = {Item{false, placementId.toString()}};
}

void PlaylistPanel::dropSample(const juce::File& file, juce::Point<int> at)
{
    auto sample = samples_.import(file);
    if (!sample)
        return;

    // A recording gets a track of its own, named after it, the way FL gives a
    // dropped sample a playlist track: two commands, one thing the user did,
    // one Ctrl+Z.
    const auto trackId = domain::TrackId::generate();
    const auto clipId = domain::AudioClipId::generate();
    const auto beats = snap(beatAtX(at.getX()), false);

    std::vector<std::unique_ptr<domain::Command>> commands;
    commands.push_back(
        std::make_unique<domain::AddTrack>(trackId, file.getFileNameWithoutExtension().toStdString(), 0.0));

    // On the line under the pointer; below the lines, on a line of its own,
    // the track's, as S16 filed it.
    const auto line = lineFor(laneAtY(at.getY()), commands);
    commands.push_back(std::make_unique<domain::PlaceAudio>(
        clipId, trackId, sample.value(), beats, line.value_or(domain::LaneId{})));

    domain::GroupOptions group{};
    group.label = "déposer " + sample.value().name;

    if (!bus_.executeGroup(std::move(commands), group).ok())
        return;

    selected_ = {Item{true, clipId.toString()}};

    // Pattern mode plays the auditioned pattern and nothing of the
    // arrangement: a clip dropped there would stay silent until SONG. A drop
    // on the playlist is a gesture on the song, so the song is what plays.
    if (state_.transport().mode == domain::PlayMode::pattern)
        static_cast<void>(bus_.execute(
            std::make_unique<domain::TransportSetMode>(domain::PlayMode::song, domain::PatternId{})));
}

void PlaylistPanel::moveSelection(double offsetBeats, int laneOffset)
{
    if (selected_.empty() || (offsetBeats == 0.0 && laneOffset == 0))
        return;

    // Never before the origin: the whole selection stops where its earliest
    // block would cross it, so the shape of the selection is kept.
    double earliest = 0.0;
    bool first = true;
    for (const auto& item : selected_)
    {
        if (const auto start = startOf(item); start.has_value())
        {
            earliest = first ? *start : std::min(earliest, *start);
            first = false;
        }
    }
    const auto offset = std::max(offsetBeats, -earliest);

    // Never above the first line either, for the same reason; below the last
    // one, the blocks that would fall off land on one new line.
    auto topmost = freeLaneCount();
    for (const auto& item : selected_)
        topmost = std::min(topmost, std::max(0, laneOf(item)));
    const auto lines = std::max(laneOffset, -topmost);
    if (offset == 0.0 && lines == 0)
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    std::optional<domain::LaneId> fresh;
    for (const auto& item : selected_)
    {
        const auto start = startOf(item);
        if (!start.has_value())
            continue;

        domain::LaneId line{};
        if (lines != 0)
        {
            const auto target = std::min(laneOf(item) + lines, freeLaneCount());
            if (target < freeLaneCount())
                line = state_.lanes()[static_cast<std::size_t>(target)].id;
            else
            {
                if (!fresh.has_value())
                    fresh = lineFor(freeLaneCount(), commands);
                line = fresh.value_or(domain::LaneId{});
            }
        }

        if (item.audio)
            commands.push_back(std::make_unique<domain::MoveAudio>(
                domain::AudioClipId::parse(item.id).value(), *start + offset, line));
        else
            commands.push_back(std::make_unique<domain::MovePlacement>(
                domain::PlacementId::parse(item.id).value(), *start + offset, line));
    }

    domain::GroupOptions group{};
    group.label = selected_.size() == 1 ? "déplacer un bloc" : "déplacer la sélection";
    static_cast<void>(bus_.executeGroup(std::move(commands), group));
}

void PlaylistPanel::removeSelection()
{
    if (selected_.empty())
        return;

    // The line of a track goes with its last clip, unless someone named it.
    std::vector<domain::AudioClipId> audio;
    std::vector<domain::PlacementId> placements;
    for (const auto& item : selected_)
    {
        if (item.audio)
            audio.push_back(domain::AudioClipId::parse(item.id).value());
        else
            placements.push_back(domain::PlacementId::parse(item.id).value());
    }

    if (laneEditing::removeBlocks(bus_, state_, audio, placements))
        selected_.clear();
}

void PlaylistPanel::copySelection()
{
    clipboard_.clear();

    double earliest = 0.0;
    bool first = true;
    for (const auto& item : selected_)
    {
        if (const auto start = startOf(item); start.has_value())
        {
            earliest = first ? *start : std::min(earliest, *start);
            first = false;
        }
    }

    for (const auto& item : selected_)
    {
        Copied copied{};
        copied.audio = item.audio;

        if (item.audio)
        {
            const auto* clip = state_.findAudioClip(domain::AudioClipId::parse(item.id).value());
            if (clip == nullptr)
                continue;
            copied.trackId = clip->trackId;
            copied.laneId = clip->laneId;
            copied.sample = clip->sample;
            copied.offsetBeats = clip->startBeats - earliest;
        }
        else
        {
            const auto* placement = state_.findPlacement(domain::PlacementId::parse(item.id).value());
            if (placement == nullptr)
                continue;
            copied.patternId = placement->patternId;
            copied.laneId = placement->laneId;
            copied.offsetBeats = placement->startBeats - earliest;
        }

        clipboard_.push_back(std::move(copied));
    }
}

void PlaylistPanel::pasteAt(double beats)
{
    if (clipboard_.empty())
        return;

    // New layings of the same content: a pasted pattern is the same pattern,
    // so editing it later changes every copy, the way it does in FL. Each on
    // the line it was copied from; a line removed since gives the block back
    // to the line of its pattern or its track.
    std::vector<std::unique_ptr<domain::Command>> commands;
    std::vector<Item> pasted;

    for (const auto& copied : clipboard_)
    {
        const auto line = state_.findLane(copied.laneId) != nullptr ? copied.laneId : domain::LaneId{};

        if (copied.audio)
        {
            if (!copied.sample.has_value() || state_.findTrack(copied.trackId) == nullptr)
                continue;

            const auto clipId = domain::AudioClipId::generate();
            commands.push_back(std::make_unique<domain::PlaceAudio>(
                clipId, copied.trackId, *copied.sample, beats + copied.offsetBeats, line));
            pasted.push_back(Item{true, clipId.toString()});
        }
        else
        {
            if (state_.findPattern(copied.patternId) == nullptr)
                continue;

            const auto placementId = domain::PlacementId::generate();
            commands.push_back(std::make_unique<domain::PlacePattern>(
                placementId, copied.patternId, beats + copied.offsetBeats, line));
            pasted.push_back(Item{false, placementId.toString()});
        }
    }

    if (commands.empty())
        return;

    domain::GroupOptions group{};
    group.label = "coller";
    if (bus_.executeGroup(std::move(commands), group).ok())
        selected_ = std::move(pasted);
}

void PlaylistPanel::duplicateSelection()
{
    if (selected_.empty())
        return;

    // Right after the selection, rounded up to the bar: what Ctrl+B does in FL.
    double earliest = 0.0;
    double latest = 0.0;
    bool first = true;
    for (const auto& item : selected_)
    {
        const auto start = startOf(item);
        if (!start.has_value())
            continue;

        earliest = first ? *start : std::min(earliest, *start);
        latest = first ? *start + lengthOf(item) : std::max(latest, *start + lengthOf(item));
        first = false;
    }
    if (first)
        return;

    copySelection();
    pasteAt(earliest + ceilToBar(latest - earliest, barBeats()));
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

void PlaylistPanel::renameLane(domain::LaneId laneId)
{
    const auto index = state_.laneIndex(laneId);
    if (!index)
        return;
    const auto& lane = state_.lanes()[index.value()];

    auto* window = new juce::AlertWindow(u8"Renommer la ligne", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", laneLabel(lane, static_cast<int>(index.value())));
    window->addButton("Renommer", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Annuler", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [this, window, laneId](int result)
                                {
                                    if (result != 1)
                                        return;

                                    const auto name = window->getTextEditorContents("name").trim();
                                    static_cast<void>(bus_.execute(
                                        std::make_unique<domain::RenameLane>(laneId, name.toStdString())));
                                }),
                            true);
}

void PlaylistPanel::showLaneMenu(int lane)
{
    if (lane < 0 || lane >= freeLaneCount())
        return;

    const auto laneId = state_.lanes()[static_cast<std::size_t>(lane)].id;

    // The pattern this line was made for, if it was: its verbs stay here,
    // where S16 had them.
    std::optional<domain::PatternId> owner;
    for (const auto& pattern : state_.patterns())
    {
        if (domain::ProjectState::laneOfPattern(pattern.id) == laneId)
            owner = pattern.id;
    }

    juce::PopupMenu menu;
    menu.addItem(renameLineItem, u8"Renommer la ligne…");
    menu.addItem(insertLineItem, u8"Insérer une ligne au-dessus");
    menu.addItem(removeLineItem, u8"Supprimer la ligne et ses blocs");
    if (owner.has_value())
    {
        menu.addSeparator();
        menu.addItem(renameItem, u8"Renommer le pattern…");
        menu.addItem(removeItem, u8"Supprimer le pattern");
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(this),
        [this, laneId, lane, owner](int chosen)
        {
            if (chosen == renameLineItem)
                renameLane(laneId);
            else if (chosen == insertLineItem)
                static_cast<void>(bus_.execute(std::make_unique<domain::CreateLane>(
                    domain::LaneId::generate(), std::string{}, static_cast<std::size_t>(lane))));
            else if (chosen == removeLineItem)
                static_cast<void>(bus_.execute(std::make_unique<domain::RemoveLane>(laneId)));
            else if (chosen == renameItem && owner.has_value())
                renamePattern(*owner);
            else if (chosen == removeItem && owner.has_value())
                static_cast<void>(bus_.execute(std::make_unique<domain::RemovePattern>(*owner)));
        });
}

void PlaylistPanel::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    pageTurn_.reset();
    zoomGlide_.reset();

    const auto point = event.getPosition();
    const auto& mods = event.mods;

    // The middle button drags the view, wherever it is pressed.
    if (mods.isMiddleButtonDown())
    {
        pan_ = Pan{point, firstBeat(), firstLanePixel()};
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    // The zone's pill first: it sits above the zone, on the ruler when the
    // zone starts on the first line.
    if (zonePill().contains(point))
    {
        openZonePrompt();
        return;
    }

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

    if (tempoMouseDown(event) || automationMouseDown(event))
        return;

    if (canvasMouseDown(event))
        return;

    const auto lane = laneAtY(point.getY());

    if (headerArea().contains(point))
    {
        if (lane < 0 || lane >= freeLaneCount())
            return;

        if (mods.isRightButtonDown())
        {
            showLaneMenu(lane);
            return;
        }

        const auto laneId = state_.lanes()[static_cast<std::size_t>(lane)].id;

        // A line made for a pattern chooses it, as its S16 lane did. Choosing
        // is not an edit: it goes to the Selection, and the rack, the piano
        // roll and pattern mode follow it from there.
        for (const auto& pattern : state_.patterns())
        {
            if (domain::ProjectState::laneOfPattern(pattern.id) == laneId)
                selection_.selectPattern(pattern.id);
        }

        // And the name can be dragged: the line moves, its blocks with it.
        laneDrag_ = LaneDrag{laneId, bus_.beginGesture("déplacer une ligne"), false};
        return;
    }

    if (!gridArea().contains(point))
        return;

    // Alt + drag draws the zone of generation.
    if (mods.isAltDown())
    {
        zoneStart_ = point;
        zone_ = zoneBetween(point, point);
        repaint();
        return;
    }

    const auto hit = itemAt(point);

    if (hit.has_value())
    {
        if (mods.isCtrlDown())
        {
            // Added to the selection, or taken out of it.
            if (isSelected(*hit))
                selected_.erase(std::remove(selected_.begin(), selected_.end(), *hit), selected_.end());
            else
                selected_.push_back(*hit);
            repaint();
            return;
        }

        if (!isSelected(*hit))
            selected_ = {*hit};

        if (mods.isRightButtonDown())
        {
            removeSelection();
            return;
        }

        if (!hit->audio)
        {
            const auto* placement = state_.findPlacement(domain::PlacementId::parse(hit->id).value());
            if (placement != nullptr)
                selection_.selectPattern(placement->patternId);
        }

        Move move{};
        move.grabBeats = beatAtX(point.getX());
        move.grabLane = lane;
        move_ = move;
        repaint();
        return;
    }

    if (mods.isCtrlDown())
    {
        bandStart_ = point;
        band_ = juce::Rectangle<int>{point, point};
        if (!mods.isShiftDown())
            selected_.clear();
        repaint();
        return;
    }

    selected_.clear();

    if (mods.isRightButtonDown() || lane < 0)
    {
        repaint();
        return;
    }

    placeAt(lane, snap(beatAtX(point.getX()), mods.isShiftDown()));
}

void PlaylistPanel::mouseDrag(const juce::MouseEvent& event)
{
    if (pan_.has_value())
    {
        const auto delta = event.getPosition() - pan_->start;
        setFirstLanePixel(pan_->firstLanePixel - delta.getY());
        setView(pan_->firstBeat - static_cast<double>(delta.getX()) / beatWidth(), zoom_);
        return;
    }

    if (tempoMouseDrag(event) || automationMouseDrag(event) || canvasMouseDrag(event))
        return;

    if (band_.has_value())
    {
        // The band as it was and as it is, with its outline: not the grid.
        const auto before = *band_;
        band_ = juce::Rectangle<int>{bandStart_, event.getPosition()}.getIntersection(gridArea());
        repaint(before.getUnion(*band_).expanded(tokens_.integer("stroke.hairline")));
        return;
    }

    if (zoneStart_.has_value())
    {
        if (auto drawn = zoneBetween(*zoneStart_, event.getPosition()); drawn.has_value())
            zone_ = drawn;
        repaint();
        return;
    }

    if (laneDrag_.has_value())
    {
        // Within the lines: the fresh one and the automation lanes are not
        // places a line can go.
        const auto grid = gridArea();
        const auto y = event.getPosition().getY();
        const auto raw = laneAtContentY(y - grid.getY() + firstLanePixel());
        const auto target = std::clamp(y < grid.getY() ? 0 : raw, 0, std::max(0, freeLaneCount() - 1));
        const auto current = state_.laneIndex(laneDrag_->id);
        if (current && static_cast<int>(current.value()) != target)
        {
            domain::ExecuteOptions options{};
            options.gesture = laneDrag_->gesture;
            if (bus_.execute(
                        std::make_unique<domain::MoveLane>(laneDrag_->id, static_cast<std::size_t>(target)),
                        options)
                    .ok())
                laneDrag_->moved = true;
        }
        return;
    }

    if (!move_.has_value())
        return;

    // From line to line as well: the lane under the pointer, relative to the
    // one grabbed. Past the lines, the fresh one.
    const auto overLane = laneAtY(event.getPosition().getY());
    const auto laneOffset =
        overLane >= 0 ? std::min(overLane, freeLaneCount()) - move_->grabLane : move_->laneOffset;

    // The move snaps as a whole, by the bar (by the beat with Shift): the
    // blocks keep their places relative to each other.
    const auto raw = beatAtX(event.getPosition().getX()) - move_->grabBeats;
    const auto step = event.mods.isShiftDown() ? 1.0 : barBeats();
    const auto offset = std::round(raw / step) * step;

    if (laneOffset == move_->laneOffset && offset == move_->offsetBeats)
        return;

    // Where the blocks were drawn and where they are drawn now; the lines and
    // the other blocks around are left as they are.
    const auto before = movingArea();
    move_->laneOffset = laneOffset;
    move_->offsetBeats = offset;
    repaint(before.getUnion(movingArea()));
}

juce::Rectangle<int> PlaylistPanel::movingArea() const
{
    juce::Rectangle<int> area;
    if (!move_.has_value())
        return area;

    // The outline of a selected block is two hairlines wide, on its edge.
    const auto outline = tokens_.integer("stroke.hairline") * 2;
    for (const auto& block : content().blocks)
    {
        if (!isSelected(block.item))
            continue;
        const auto drawn = blockBounds(block, move_->offsetBeats, move_->laneOffset).expanded(outline);
        area = area.isEmpty() ? drawn : area.getUnion(drawn);
    }
    return area.getIntersection(gridArea());
}

void PlaylistPanel::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (pan_.has_value())
    {
        pan_.reset();
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    if (tempoMouseUp() || automationMouseUp() || canvasMouseUp())
        return;

    if (zoneStart_.has_value())
    {
        zoneStart_.reset();
        return;
    }

    if (laneDrag_.has_value())
    {
        static_cast<void>(bus_.endGesture(laneDrag_->gesture));
        laneDrag_.reset();
        return;
    }

    if (band_.has_value())
    {
        const auto area = *band_;
        band_.reset();

        for (const auto& block : content().blocks)
        {
            if (blockBounds(block, 0.0, 0).intersects(area) && !isSelected(block.item))
                selected_.push_back(block.item);
        }
        repaint();
        return;
    }

    if (!move_.has_value())
        return;

    const auto offset = move_->offsetBeats;
    const auto lines = move_->laneOffset;
    move_.reset();
    moveSelection(offset, lines);
    repaint();
}

void PlaylistPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (tempoDoubleClick(event.getPosition()) || automationDoubleClick(event.getPosition()) ||
        canvasDoubleClick(event.getPosition()))
        return;

    if (!headerArea().contains(event.getPosition()))
        return;

    const auto lane = laneAtY(event.getPosition().getY());
    if (lane >= 0 && lane < freeLaneCount())
        renameLane(state_.lanes()[static_cast<std::size_t>(lane)].id);
}

bool PlaylistPanel::keyPressed(const juce::KeyPress& key)
{
    const auto ctrl = juce::ModifierKeys::ctrlModifier;

    if (zoneKey(key) || canvasKey(key))
        return true;

    if (key == juce::KeyPress{'c', ctrl, 0})
    {
        copySelection();
        return true;
    }

    if (key == juce::KeyPress{'v', ctrl, 0})
    {
        // At the playhead, on its bar: where the song is being listened to.
        // While playing, the engine's clock says where that is; stopped, the
        // domain does — the ruler, Stop and the copilot all set it there, and
        // the engine only follows.
        const auto position = clock_.isPlaying() ? clock_.positionBeats() : state_.transport().positionBeats;
        pasteAt(snap(position, false));
        return true;
    }

    if (key == juce::KeyPress{'b', ctrl, 0})
    {
        duplicateSelection();
        return true;
    }

    if (key == juce::KeyPress{juce::KeyPress::deleteKey} ||
        key == juce::KeyPress{juce::KeyPress::backspaceKey})
    {
        removeSelection();
        return true;
    }

    return false;
}

// --- dropping samples ---------------------------------------------------------

bool PlaylistPanel::isInterestedInDragSource(const SourceDetails& details)
{
    return sampleFrom(details).has_value();
}

void PlaylistPanel::itemDropped(const SourceDetails& details)
{
    if (const auto file = sampleFrom(details); file.has_value())
        dropSample(*file, details.localPosition);
}

bool PlaylistPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    return std::any_of(files.begin(),
                       files.end(),
                       [](const juce::String& path) { return SampleHost::isSampleFile(juce::File{path}); });
}

void PlaylistPanel::filesDropped(const juce::StringArray& files, int x, int y)
{
    for (const auto& path : files)
    {
        const juce::File file{path};
        if (SampleHost::isSampleFile(file))
        {
            dropSample(file, {x, y});
            return;
        }
    }
}

} // namespace daw::ui
