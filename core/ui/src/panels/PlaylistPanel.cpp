#include "daw/ui/panels/PlaylistPanel.h"

#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
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

// A notch of the wheel is about a quarter of a unit in JUCE. Zoom doubles over
// four notches; a scroll moves an eighth of the view per notch, the lanes two
// lanes' worth.
constexpr double zoomPerWheelUnit = 2.0;
constexpr double viewPerWheelUnit = 0.5;
constexpr double lanesPerWheelUnit = 8.0;

// How far right the view may go: 999 bars. The timeline itself ends four bars
// after the song, but a person lays the next thing beyond it, the way FL's
// playlist lets them: scrolling past the end extends the view.
constexpr double longestViewBeats = 999.0 * beatsPerBar;

enum LaneMenu
{
    renameItem = 1,
    removeItem = 2
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

[[nodiscard]] double ceilToBar(double beats)
{
    return std::ceil(beats / beatsPerBar - 1e-9) * beatsPerBar;
}

} // namespace

PlaylistPanel::PlaylistPanel(const PanelContext& context)
    : tokens_(context.tokens)
    , lookAndFeel_(context.lookAndFeel)
    , bus_(context.bus)
    , state_(context.state)
    , project_(context.project)
    , selection_(context.selection)
    , clock_(context.clock)
    , samples_(context.samples)
{
    titled_ = context.titled;
    setLookAndFeel(&lookAndFeel_);

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
    startTimer(playheadRefreshMs);
}

PlaylistPanel::~PlaylistPanel()
{
    stopTimer();
    horizontal_.removeListener(this);
    vertical_.removeListener(this);
    selection_.removeChangeListener(this);
    project_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void PlaylistPanel::resized()
{
    const auto thickness = tokens_.integer("metric.scrollbar.thickness");
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));

    auto bottom = area.removeFromBottom(thickness);
    bottom.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    bottom.removeFromRight(thickness);
    horizontal_.setBounds(bottom);

    area.removeFromTop(tokens_.integer("metric.playlist.rulerHeight"));
    vertical_.setBounds(area.removeFromRight(thickness));

    updateScrollBars();
}

void PlaylistPanel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused(source);

    // A block an undo took away is no longer selectable.
    const auto all = items();
    selected_.erase(std::remove_if(selected_.begin(),
                                   selected_.end(),
                                   [&all](const Item& item)
                                   { return std::find(all.begin(), all.end(), item) == all.end(); }),
                    selected_.end());

    // A song that grew or shrank moves the bounds of the view: an undo that
    // takes the last bars away must not leave the view past the end.
    updateScrollBars();
    repaint();
}

// --- lanes ------------------------------------------------------------------

int PlaylistPanel::patternLaneCount() const
{
    return static_cast<int>(state_.patterns().size());
}

std::vector<domain::TrackId> PlaylistPanel::audioTracks() const
{
    // In the order of the track list, one lane per track that holds audio.
    std::vector<domain::TrackId> tracks;
    for (const auto& track : state_.tracks())
    {
        const auto holds =
            std::any_of(state_.audioClips().begin(),
                        state_.audioClips().end(),
                        [&track](const domain::AudioClip& clip) { return clip.trackId == track.id; });
        if (holds)
            tracks.push_back(track.id);
    }
    return tracks;
}

int PlaylistPanel::laneCount() const
{
    return patternLaneCount() + static_cast<int>(audioTracks().size());
}

int PlaylistPanel::laneOfTrack(domain::TrackId trackId) const
{
    const auto tracks = audioTracks();
    const auto found = std::find(tracks.begin(), tracks.end(), trackId);
    return found == tracks.end()
               ? -1
               : patternLaneCount() + static_cast<int>(std::distance(tracks.begin(), found));
}

// --- geometry ---------------------------------------------------------------

juce::Rectangle<int> PlaylistPanel::headerArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                       tokens_.integer("metric.playlist.rulerHeight"));
    area.removeFromBottom(tokens_.integer("metric.scrollbar.thickness"));
    return area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
}

juce::Rectangle<int> PlaylistPanel::rulerArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight"));
    area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    area.removeFromRight(tokens_.integer("metric.scrollbar.thickness"));
    return area.removeFromTop(tokens_.integer("metric.playlist.rulerHeight"));
}

juce::Rectangle<int> PlaylistPanel::gridArea() const
{
    auto area = getLocalBounds();
    area.removeFromTop(tokens_.integer("metric.panel.headerHeight") +
                       tokens_.integer("metric.playlist.rulerHeight"));
    area.removeFromLeft(tokens_.integer("metric.playlist.headerWidth"));
    area.removeFromRight(tokens_.integer("metric.scrollbar.thickness"));
    area.removeFromBottom(tokens_.integer("metric.scrollbar.thickness"));
    return area;
}

double PlaylistPanel::timelineBeats() const
{
    double end = 0.0;
    for (const auto& item : items())
    {
        if (const auto start = startOf(item); start.has_value())
            end = std::max(end, *start + lengthOf(item));
    }

    const auto wanted = std::max(minimumVisibleBeats, end + roomAfterSongBeats);
    return std::ceil(wanted / beatsPerBar) * beatsPerBar;
}

double PlaylistPanel::fitBeatWidth() const
{
    const auto width = static_cast<double>(std::max(1, gridArea().getWidth()));
    const auto floor = static_cast<double>(tokens_.integer("metric.playlist.beatWidthMin"));
    return std::max(floor, width / timelineBeats());
}

double PlaylistPanel::beatWidth() const
{
    const auto fit = fitBeatWidth();
    const auto widest = std::max(fit, static_cast<double>(tokens_.integer("metric.playlist.beatWidthMax")));
    return std::clamp(zoom_.value_or(fit), fit, widest);
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
    return laneCount() * tokens_.integer("metric.playlist.laneHeight");
}

int PlaylistPanel::firstLanePixel() const
{
    return std::clamp(firstLanePixel_, 0, std::max(0, lanesHeight() - gridArea().getHeight()));
}

juce::Point<int> PlaylistPanel::pointFor(int lane, double beats) const
{
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    return {xForBeat(beats), gridArea().getY() + lane * laneHeight + laneHeight / 2 - firstLanePixel()};
}

int PlaylistPanel::laneAtY(int y) const
{
    const auto grid = gridArea();
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    if (y < grid.getY() || y >= grid.getBottom() || laneHeight <= 0)
        return -1;

    const auto lane = (y - grid.getY() + firstLanePixel()) / laneHeight;
    return lane < laneCount() ? lane : -1;
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
    horizontal_.setSingleStepSize(static_cast<double>(beatsPerBar));

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
    if (bar == &horizontal_)
        setView(newRangeStart, zoom_);
    else if (bar == &vertical_)
        setFirstLanePixel(static_cast<int>(std::lround(newRangeStart)));
}

void PlaylistPanel::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto grid = gridArea();

    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
    {
        // Around the pointer: the beat under it stays under it.
        const auto x = std::clamp(event.getPosition().getX(), grid.getX(), grid.getRight());
        const auto anchor = beatAtX(x);
        const auto width = beatWidth() * std::pow(zoomPerWheelUnit, static_cast<double>(wheel.deltaY));
        const auto widest =
            std::max(fitBeatWidth(), static_cast<double>(tokens_.integer("metric.playlist.beatWidthMax")));
        const auto clamped = std::clamp(width, fitBeatWidth(), widest);
        setView(anchor - static_cast<double>(x - grid.getX()) / clamped, clamped);
        return;
    }

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
    // edge, on a bar line.
    setView(std::floor(beats / beatsPerBar) * beatsPerBar, zoom_);
}

double PlaylistPanel::snap(double beats, bool fine)
{
    const auto step = fine ? 1.0 : static_cast<double>(beatsPerBar);
    return std::max(0.0, std::floor(beats / step) * step);
}

// --- items ------------------------------------------------------------------

std::vector<PlaylistPanel::Item> PlaylistPanel::items() const
{
    std::vector<Item> all;
    for (const auto& placement : state_.arrangement())
        all.push_back(Item{false, placement.id.toString()});
    for (const auto& clip : state_.audioClips())
        all.push_back(Item{true, clip.id.toString()});
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
    if (item.audio)
    {
        const auto id = domain::AudioClipId::parse(item.id);
        const auto* clip = id ? state_.findAudioClip(id.value()) : nullptr;
        return clip != nullptr ? laneOfTrack(clip->trackId) : -1;
    }

    const auto id = domain::PlacementId::parse(item.id);
    const auto* placement = id ? state_.findPlacement(id.value()) : nullptr;
    if (placement == nullptr)
        return -1;

    const auto index = state_.patternIndex(placement->patternId);
    return index ? static_cast<int>(index.value()) : -1;
}

juce::Rectangle<int> PlaylistPanel::bounds(const Item& item, double offsetBeats) const
{
    const auto start = startOf(item);
    const auto lane = laneOf(item);
    if (!start.has_value() || lane < 0)
        return {};

    const auto grid = gridArea();
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    const auto left = xForBeat(std::max(0.0, *start + offsetBeats));
    const auto right = xForBeat(std::max(0.0, *start + offsetBeats) + lengthOf(item));

    return juce::Rectangle<int>{
        left, grid.getY() + lane * laneHeight - firstLanePixel(), std::max(1, right - left), laneHeight}
        .withTrimmedTop(tokens_.integer("metric.playlist.blockInset"))
        .withTrimmedBottom(tokens_.integer("metric.playlist.blockInset"));
}

std::optional<PlaylistPanel::Item> PlaylistPanel::itemAt(juce::Point<int> point) const
{
    // The last one drawn wins, which is the one on top.
    std::optional<Item> found;
    for (const auto& item : items())
    {
        if (bounds(item).contains(point))
            found = item;
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
        g.drawText("PLAYLIST", header, juce::Justification::centredLeft, false);

    if (laneCount() == 0)
    {
        paintEmpty(g);
        return;
    }

    paintRuler(g, rulerArea());
    paintLanes(g, gridArea(), headerArea());
    paintBlocks(g, gridArea());
    paintPlayhead(g);

    if (band_.has_value())
    {
        g.setColour(tokens_.colour("color.state.selected"));
        g.fillRect(*band_);
        g.setColour(tokens_.colour("color.accent.primary"));
        g.drawRect(*band_, tokens_.integer("stroke.hairline"));
    }
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
    const auto barWidth = static_cast<int>(beatWidth() * beatsPerBar);
    const auto roomy = tokens_.integer("space.xl");
    const auto every = barWidth >= roomy ? 1 : (barWidth * 4 >= roomy ? 4 : 16);

    const auto firstBar = static_cast<int>(firstBeat()) / beatsPerBar / every * every;
    const auto lastBar = static_cast<int>(std::ceil((firstBeat() + viewBeats()) / beatsPerBar));

    for (int bar = firstBar; bar <= lastBar; bar += every)
    {
        const auto x = xForBeat(static_cast<double>(bar * beatsPerBar));
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.drawText(juce::String(bar + 1),
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
    const auto laneHeight = tokens_.integer("metric.playlist.laneHeight");
    const auto hairline = tokens_.integer("stroke.hairline");
    const auto* shown = patternEditing::current(state_, selection_);

    g.saveState();
    g.reduceClipRegion(grid.getUnion(headers));

    // Bar lines first, under everything, and only the ones in sight.
    const auto firstBar = static_cast<int>(firstBeat()) / beatsPerBar;
    const auto lastBar = static_cast<int>(std::ceil((firstBeat() + viewBeats()) / beatsPerBar));
    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        const auto x = xForBeat(static_cast<double>(bar * beatsPerBar));
        g.setColour(tokens_.colour(bar % 4 == 0 ? "color.grid.bar" : "color.grid.beat"));
        g.fillRect(x, grid.getY(), hairline, grid.getHeight());
    }

    g.setColour(tokens_.colour("color.surface.panel"));
    g.fillRect(headers);

    const auto tracks = audioTracks();
    for (int lane = 0; lane < laneCount(); ++lane)
    {
        const auto y = grid.getY() + lane * laneHeight - firstLanePixel();
        if (y + laneHeight <= grid.getY())
            continue;
        if (y >= grid.getBottom())
            break;

        auto name = headers.withY(y).withHeight(laneHeight);
        juce::String label;

        if (lane < patternLaneCount())
        {
            const auto& pattern = state_.patterns()[static_cast<std::size_t>(lane)];
            label = juce::String::fromUTF8(patternEditing::displayName(state_, pattern).c_str());

            // The pattern the rack and the piano roll show is the lit lane:
            // one choice, three screens.
            if (shown != nullptr && pattern.id == shown->id)
            {
                g.setColour(tokens_.colour("color.state.selected"));
                g.fillRect(name);
            }
        }
        else if (const auto* track =
                     state_.findTrack(tracks[static_cast<std::size_t>(lane - patternLaneCount())]);
                 track != nullptr)
        {
            label = juce::String::fromUTF8(track->name.c_str());
        }

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(grid.getX(), y + laneHeight - hairline, grid.getWidth(), hairline);
        g.fillRect(name.getX(), name.getBottom() - hairline, name.getWidth(), hairline);

        g.setColour(
            tokens_.colour(lane < patternLaneCount() ? "color.text.primary" : "color.text.secondary"));
        g.setFont(lookAndFeel_.typography().sans("font.size.caption", "font.weight.medium"));
        g.drawText(
            label, name.reduced(tokens_.integer("space.sm"), 0), juce::Justification::centredLeft, true);
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

    for (const auto& item : items())
    {
        const auto moving = move_.has_value() && isSelected(item);
        const auto block = bounds(item, moving ? move_->offsetBeats : 0.0);
        if (block.isEmpty() || !block.intersects(grid))
            continue;

        juce::String label;
        bool lit = false;

        if (item.audio)
        {
            const auto id = domain::AudioClipId::parse(item.id);
            if (const auto* clip = id ? state_.findAudioClip(id.value()) : nullptr; clip != nullptr)
                label = juce::String::fromUTF8(clip->sample.name.c_str());
        }
        else
        {
            const auto id = domain::PlacementId::parse(item.id);
            const auto* placement = id ? state_.findPlacement(id.value()) : nullptr;
            const auto* pattern = placement != nullptr ? state_.findPattern(placement->patternId) : nullptr;
            if (pattern != nullptr)
            {
                label = juce::String::fromUTF8(patternEditing::displayName(state_, *pattern).c_str());

                // The layings of the pattern being edited are lit: an edit in
                // the rack lands in every one of them, and the screen says so
                // before the ear does.
                lit = shown != nullptr && pattern->id == shown->id;
            }
        }

        const auto* fill =
            item.audio ? "color.actor.copilot" : (lit ? "color.note.fill" : "color.note.fillSoft");
        g.setColour(tokens_.colour(fill));
        g.fillRoundedRectangle(block.toFloat(), radius);

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
        g.drawText(
            label, block.reduced(tokens_.integer("space.xs"), 0), juce::Justification::centredLeft, true);
    }

    g.restoreState();
}

std::optional<int> PlaylistPanel::playheadX() const
{
    if (state_.transport().mode != domain::PlayMode::song)
        return {};

    const auto beats = clock_.positionBeats();
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

void PlaylistPanel::timerCallback()
{
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
}

// --- editing ----------------------------------------------------------------

void PlaylistPanel::placeAt(int lane, double beats)
{
    if (lane < 0 || lane >= patternLaneCount())
        return;

    const auto patternId = state_.patterns()[static_cast<std::size_t>(lane)].id;

    // The identifier is drawn here, by the caller, like every identifier in
    // this project: the command engenders none.
    const auto placementId = domain::PlacementId::generate();
    if (bus_.execute(std::make_unique<domain::PlacePattern>(placementId, patternId, beats)).ok())
        selected_ = {Item{false, placementId.toString()}};

    selection_.selectPattern(patternId);
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
    commands.push_back(std::make_unique<domain::PlaceAudio>(clipId, trackId, sample.value(), beats));

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

void PlaylistPanel::moveSelection(double offsetBeats)
{
    if (selected_.empty() || offsetBeats == 0.0)
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
    if (offset == 0.0)
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    for (const auto& item : selected_)
    {
        const auto start = startOf(item);
        if (!start.has_value())
            continue;

        if (item.audio)
            commands.push_back(std::make_unique<domain::MoveAudio>(
                domain::AudioClipId::parse(item.id).value(), *start + offset));
        else
            commands.push_back(std::make_unique<domain::MovePlacement>(
                domain::PlacementId::parse(item.id).value(), *start + offset));
    }

    domain::GroupOptions group{};
    group.label = selected_.size() == 1 ? "déplacer un bloc" : "déplacer la sélection";
    static_cast<void>(bus_.executeGroup(std::move(commands), group));
}

void PlaylistPanel::removeSelection()
{
    if (selected_.empty())
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    for (const auto& item : selected_)
    {
        if (item.audio)
            commands.push_back(
                std::make_unique<domain::RemoveAudio>(domain::AudioClipId::parse(item.id).value()));
        else
            commands.push_back(
                std::make_unique<domain::RemovePlacement>(domain::PlacementId::parse(item.id).value()));
    }

    domain::GroupOptions group{};
    group.label = selected_.size() == 1 ? "retirer un bloc" : "retirer la sélection";
    if (bus_.executeGroup(std::move(commands), group).ok())
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
            copied.sample = clip->sample;
            copied.offsetBeats = clip->startBeats - earliest;
        }
        else
        {
            const auto* placement = state_.findPlacement(domain::PlacementId::parse(item.id).value());
            if (placement == nullptr)
                continue;
            copied.patternId = placement->patternId;
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
    // so editing it later changes every copy, the way it does in FL.
    std::vector<std::unique_ptr<domain::Command>> commands;
    std::vector<Item> pasted;

    for (const auto& copied : clipboard_)
    {
        if (copied.audio)
        {
            if (!copied.sample.has_value() || state_.findTrack(copied.trackId) == nullptr)
                continue;

            const auto clipId = domain::AudioClipId::generate();
            commands.push_back(std::make_unique<domain::PlaceAudio>(
                clipId, copied.trackId, *copied.sample, beats + copied.offsetBeats));
            pasted.push_back(Item{true, clipId.toString()});
        }
        else
        {
            if (state_.findPattern(copied.patternId) == nullptr)
                continue;

            const auto placementId = domain::PlacementId::generate();
            commands.push_back(std::make_unique<domain::PlacePattern>(
                placementId, copied.patternId, beats + copied.offsetBeats));
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
    pasteAt(earliest + ceilToBar(latest - earliest));
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
    if (lane < 0 || lane >= patternLaneCount())
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
    grabKeyboardFocus();

    const auto point = event.getPosition();
    const auto& mods = event.mods;

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
        if (lane < 0 || lane >= patternLaneCount())
            return;

        if (mods.isRightButtonDown())
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
    if (band_.has_value())
    {
        band_ = juce::Rectangle<int>{bandStart_, event.getPosition()}.getIntersection(gridArea());
        repaint();
        return;
    }

    if (!move_.has_value())
        return;

    // The move snaps as a whole, by the bar (by the beat with Shift): the
    // blocks keep their places relative to each other.
    const auto raw = beatAtX(event.getPosition().getX()) - move_->grabBeats;
    const auto step = event.mods.isShiftDown() ? 1.0 : static_cast<double>(beatsPerBar);
    const auto offset = std::round(raw / step) * step;

    if (offset != move_->offsetBeats)
    {
        move_->offsetBeats = offset;
        repaint();
    }
}

void PlaylistPanel::mouseUp(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);

    if (band_.has_value())
    {
        const auto area = *band_;
        band_.reset();

        for (const auto& item : items())
        {
            if (bounds(item).intersects(area) && !isSelected(item))
                selected_.push_back(item);
        }
        repaint();
        return;
    }

    if (!move_.has_value())
        return;

    const auto offset = move_->offsetBeats;
    move_.reset();
    moveSelection(offset);
    repaint();
}

void PlaylistPanel::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (!headerArea().contains(event.getPosition()))
        return;

    const auto lane = laneAtY(event.getPosition().getY());
    if (lane >= 0 && lane < patternLaneCount())
        renamePattern(state_.patterns()[static_cast<std::size_t>(lane)].id);
}

bool PlaylistPanel::keyPressed(const juce::KeyPress& key)
{
    const auto ctrl = juce::ModifierKeys::ctrlModifier;

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
