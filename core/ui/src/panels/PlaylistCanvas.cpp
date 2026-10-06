// The canvas (S18): the playlist, zoomed on into the notes.
//
// Each line unfolds into bands, one per track its blocks play, framed on what
// the track plays there (CanvasBands). The zoom grows both axes at once: a
// sixteenth and a row reach the size of a note that can be aimed at together.
// On the way there the grid of each band fades in and the notes take their
// colour, so what can be grabbed is seen before it is clicked.
//
// Past the threshold, in a block, under its name:
//   click on nothing             writes a sixteenth there, in that band's track
//   drag a note                  moves it, in time and pitch, one history entry
//   drag its right edge          stretches it
//   right-click a note           removes it
//   Ctrl + click                 picks a note, or takes it out of the picking
//   Ctrl + drag                  a band that picks every note it touches, across
//                                blocks and lines (Shift keeps the picking); above
//                                the threshold the band picks blocks, as in the
//                                playlist, and which it is is decided when it starts
//   Delete                       removes the picked notes
//   Ctrl+C                       copies the picked notes, by value, the gaps
//                                between them kept in song time (S19)
//   Ctrl+V                       pastes them in the block under the hand, at the
//                                sixteenth under the hand, each row on its track;
//                                what falls past the pattern is left out, and the
//                                history entry says how many
//   Ctrl+B                       duplicates them right after themselves, in their
//                                pattern, lengthened to the bar (one pattern only)
//   in the velocity strip        under the band of the channel chosen in the rack
//                                (or by touching a note), at the scale of notes
//                                only: a click sets the stems under it, a drag
//                                draws a line over them, sent as one entry on
//                                release; two picked notes or more: only those
//   Alt + wheel on a note        its velocity, finely; one entry per gesture
//   Ctrl+Q                       quantises the picked notes to the sixteenth
//   Up, Down                     transposes them a semitone; with Ctrl, an octave.
//                                A note pushed off the keyboard refuses it all
//   drag the edge of a band      adds rows above or below, for this screen
//   double-click the name strip  frames the block
//   F                            frames the selected blocks, else the block
//                                under the hand; Shift+F shows the song
//   Escape                       lets go of the picked notes
// The name strip keeps the block's own gestures: move it, select it, remove it.
//
// A note belongs to the pattern, not to the block: while the hand is on a
// block, the other blocks of its pattern are lit, and they change with it.
//
// Nothing here is state of the project: the zoom, the rows added by hand and
// the picked notes are the screen's (S9 §6.4). Every edit leaves as a command.

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/ui/model/NoteClipboard.h"
#include "daw/ui/model/PatternEditing.h"
#include "daw/ui/model/ViewFraming.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace daw::ui
{
namespace
{

// One sixteenth, the grid the piano roll snaps to.
constexpr double stepBeats = 0.25;
constexpr int defaultVelocity = 100;

// A framed block leaves this much of the width free on its left.
constexpr double frameMargin = 0.05;

// Alt + wheel on a note: the gesture ends when the wheel has rested this long.
constexpr juce::uint32 velocityWheelRestMs = 600;

// A block drawn once and laid many times: up to four megapixels an image, a
// few hundred images kept. Past that, the block is drawn where it lies.
constexpr std::int64_t canvasImagePixels = 4'000'000;
constexpr std::size_t canvasImageCount = 256;

[[nodiscard]] bool isBlackKey(int pitch) noexcept
{
    switch (((pitch % 12) + 12) % 12)
    {
    case 1:
    case 3:
    case 6:
    case 8:
    case 10:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::pair<std::string, std::string> keyOf(domain::LaneId lane, domain::TrackId track)
{
    return {lane.toString(), track.toString()};
}

} // namespace

// --- the scale -----------------------------------------------------------------

canvas::Scale PlaylistPanel::canvasScale() const
{
    canvas::Scale scale{};
    scale.grabRow = static_cast<double>(tokens_.number("metric.canvas.rowGrab"));
    scale.grabStep = static_cast<double>(tokens_.number("metric.canvas.stepGrab"));
    scale.maxRow = static_cast<double>(tokens_.number("metric.canvas.rowMax"));
    scale.approachRow = static_cast<double>(tokens_.number("metric.canvas.rowApproach"));
    return scale;
}

double PlaylistPanel::canvasRow() const
{
    return canvas::rowHeight(beatWidth(), canvasScale());
}

int PlaylistPanel::canvasChrome() const
{
    return tokens_.integer("metric.playlist.blockInset") * 2 +
           tokens_.integer("metric.playlist.labelHeight") + tokens_.integer("stroke.hairline");
}

bool PlaylistPanel::notesGrabbable() const
{
    return canvas_ && canvas::grabbable(beatWidth(), canvasRow(), canvasScale());
}

// --- the pattern mode ---------------------------------------------------------------

bool PlaylistPanel::patternMode() const
{
    return canvas_ && state_.transport().mode == domain::PlayMode::pattern &&
           patternEditing::current(state_, selection_) != nullptr;
}

std::optional<domain::Placement> PlaylistPanel::shownPlacement(domain::PlacementId placementId) const
{
    if (patternMode())
    {
        if (placementId != patternBlock_)
            return std::nullopt;
        domain::Placement block{};
        block.id = patternBlock_;
        block.patternId = patternEditing::current(state_, selection_)->id;
        block.startBeats = 0.0;
        block.laneId = patternLine_;
        return block;
    }
    if (const auto* placement = state_.findPlacement(placementId); placement != nullptr)
        return *placement;
    return std::nullopt;
}

std::optional<int> PlaylistPanel::shownLaneIndex(domain::LaneId laneId) const
{
    if (patternMode())
        return laneId == patternLine_ ? std::optional<int>{0} : std::nullopt;
    const auto index = state_.laneIndex(laneId);
    return index ? std::optional<int>{static_cast<int>(index.value())} : std::nullopt;
}

domain::LaneId PlaylistPanel::lineId(int lane) const
{
    if (patternMode())
        return patternLine_;
    return state_.lanes()[static_cast<std::size_t>(lane)].id;
}

// Called on every change heard. True when the canvas changed what it shows:
// the view of the mode left is kept, the other one found again, and a
// pattern seen for the first time is framed at the scale of notes.
bool PlaylistPanel::followMode()
{
    const auto pattern = patternMode();
    const auto* shown = pattern ? patternEditing::current(state_, selection_) : nullptr;
    const auto shownId = shown != nullptr ? shown->id : domain::PatternId{};
    if (pattern == showingPattern_ && shownId == showingPatternId_)
        return false;

    auto& left = showingPattern_ ? patternView_ : songView_;
    left = SavedView{true, firstBeat(), zoom_, firstLanePixel()};

    const auto otherPattern = pattern && shownId != showingPatternId_;
    showingPattern_ = pattern;
    showingPatternId_ = shownId;
    pickedNotes_.clear();
    selected_.clear();
    frozen_.reset();
    closeBand();
    bandRange_.reset();
    // A block lit by the hand in the other mode is not on this screen.
    hover(std::nullopt);
    ++version_;
    updateScrollBars();

    const auto& found = pattern ? patternView_ : songView_;
    if (pattern && (otherPattern || !found.saved))
        frameBlock(patternBlock_);
    else if (found.saved)
    {
        setView(found.firstBeat, found.zoom);
        setFirstLanePixel(found.lanePixel);
    }
    return true;
}

// --- the lines -----------------------------------------------------------------

const std::vector<int>& PlaylistPanel::laneEdges() const
{
    const auto width = beatWidth();
    if (!edges_.empty() && edgesVersion_ == version_ && edgesWidth_ == width)
        return edges_;

    const auto all = lanes();
    edges_.assign(all.size() + 1, 0);
    for (std::size_t lane = 0; lane < all.size(); ++lane)
        edges_[lane + 1] = edges_[lane] + computedLaneHeight(static_cast<int>(lane), all[lane]);
    edgesVersion_ = version_;
    edgesWidth_ = width;
    return edges_;
}

int PlaylistPanel::computedLaneHeight(int lane, const Lane& entry) const
{
    const auto base = tokens_.integer("metric.playlist.laneHeight");
    if (entry.kind != Lane::Kind::line)
        return base;
    const auto bands = bandsOfLane(lane);
    if (bands.empty())
        return base;
    return canvas::lineHeight(canvas::rowCount(bands),
                              canvasRow(),
                              canvasChrome() + velocityStripOf(bands) + addBandOf(lane, bands),
                              base);
}

// The strip opens under the band of the chosen track, at the scale of notes:
// one at a time on a line, so it costs its height once (S19).
int PlaylistPanel::velocityStripOf(const std::vector<CanvasBand>& bands) const
{
    if (!notesGrabbable() || selection_.track().isNil())
        return 0;
    const auto chosen =
        std::any_of(bands.begin(),
                    bands.end(),
                    [this](const CanvasBand& band) { return band.track == selection_.track(); });
    return chosen ? tokens_.integer("metric.canvas.velocityStrip") : 0;
}

std::vector<CanvasBand> PlaylistPanel::bandsOfLane(int lane) const
{
    if (lane < 0 || lane >= freeLaneCount())
        return {};

    const auto laneId = lineId(lane);
    if (frozen_.has_value() && frozen_->first == laneId)
        return frozen_->second;

    std::vector<CanvasBand> bands;
    if (patternMode())
    {
        if (const auto* shown = patternEditing::current(state_, selection_); shown != nullptr)
            bands = bands_.ofPattern(state_, shown->id);
    }
    else
        bands = bands_.of(laneId);

    // A proposal shown in a band: the band reaches its notes.
    if (bandGen_.has_value() && !bandGen_->ghosts.empty())
    {
        const auto& where = bandGen_->where;
        const auto onThisLine = patternMode() || std::any_of(state_.arrangement().begin(),
                                                             state_.arrangement().end(),
                                                             [&](const domain::Placement& placement) {
                                                                 return placement.laneId == laneId &&
                                                                        placement.patternId == where.pattern;
                                                             });
        for (auto& band : bands)
        {
            if (!onThisLine || band.track != where.track)
                continue;
            for (const auto& ghost : bandGen_->ghosts)
            {
                band.low = std::min(band.low, ghost.pitch);
                band.high = std::max(band.high, ghost.pitch);
            }
        }
    }
    for (auto& band : bands)
    {
        if (const auto found = extensions_.find(keyOf(laneId, band.track)); found != extensions_.end())
            band = canvas::extended(band, found->second);
    }
    return bands;
}

std::vector<PlaylistPanel::BandArea> PlaylistPanel::bandAreas(int lane) const
{
    const auto bands = bandsOfLane(lane);
    if (bands.empty())
        return {};

    const auto grid = gridArea();
    const auto strip = velocityStripOf(bands);
    const auto chrome = canvasChrome() + strip + addBandOf(lane, bands);
    const auto height = laneHeightOf(lane);
    const auto row = canvas::fittedRow(canvas::rowCount(bands), canvasRow(), chrome, height);

    auto y = static_cast<double>(
        grid.getY() + laneTop(lane) - firstLanePixel() + tokens_.integer("metric.playlist.blockInset") +
        tokens_.integer("metric.playlist.labelHeight") + tokens_.integer("stroke.hairline"));

    std::vector<BandArea> areas;
    for (const auto& band : bands)
    {
        const auto next = y + static_cast<double>(band.rows()) * row;
        const auto top = static_cast<int>(std::lround(y));
        const auto bottom = static_cast<int>(std::lround(next));
        areas.push_back(
            BandArea{band, {grid.getX(), top, grid.getWidth(), std::max(1, bottom - top)}, row, {}});
        y = next;
        if (strip > 0 && band.track == selection_.track())
        {
            areas.back().velocity = {grid.getX(), bottom, grid.getWidth(), strip};
            y += static_cast<double>(strip);
        }
    }
    return areas;
}

void PlaylistPanel::contentChanged()
{
    static_cast<void>(bands_.refresh(state_));
    ++version_;

    // A note an undo took away is no longer picked.
    const auto exists = [this](domain::ClipId clip, domain::NoteId note)
    {
        for (const auto& pattern : state_.patterns())
        {
            for (const auto& row : pattern.clips)
            {
                if (row.id != clip)
                    continue;
                return std::any_of(row.notes.begin(),
                                   row.notes.end(),
                                   [note](const domain::Note& candidate) { return candidate.id == note; });
            }
        }
        return false;
    };
    pickedNotes_.erase(std::remove_if(pickedNotes_.begin(),
                                      pickedNotes_.end(),
                                      [&](const PickedNote& picked)
                                      { return !exists(picked.clip, picked.note); }),
                       pickedNotes_.end());

    if (!hoveredPlacement_.isNil() && !shownPlacement(hoveredPlacement_).has_value())
        hover(std::nullopt);
}

// A command run in the same call as the hand — a script, a verification — has
// not been heard yet: the bands follow the project before the hand is read.
void PlaylistPanel::catchUp()
{
    if (!canvas_ || project_.revision() == handledRevision_)
        return;
    handledRevision_ = project_.revision();
    static_cast<void>(previews_.refresh(state_));
    contentChanged();
    updateScrollBars();
    repaint();
}

// --- what is under the hand ------------------------------------------------------

juce::Rectangle<int> PlaylistPanel::noteRect(const BandArea& band,
                                             double blockStart,
                                             double patternLength,
                                             const domain::Note& note) const
{
    const auto end = std::min(note.startBeats + note.lengthBeats, patternLength);
    const auto left = xForBeat(blockStart + note.startBeats);
    const auto right = xForBeat(blockStart + end);
    const auto fromTop = static_cast<double>(band.band.high - note.pitch);
    const auto top = band.area.getY() + static_cast<int>(std::lround(fromTop * band.row));
    const auto bottom = band.area.getY() + static_cast<int>(std::lround((fromTop + 1.0) * band.row));
    return {left, top, std::max(1, right - left), std::max(1, bottom - top)};
}

int PlaylistPanel::pitchAt(const BandArea& band, int y) const
{
    const auto rows = static_cast<int>(std::floor(static_cast<double>(y - band.area.getY()) / band.row));
    return std::clamp(band.band.high - rows, band.band.low, band.band.high);
}

bool PlaylistPanel::isPickedNote(domain::NoteId note) const
{
    return std::any_of(pickedNotes_.begin(),
                       pickedNotes_.end(),
                       [note](const PickedNote& picked) { return picked.note == note; });
}

std::optional<PlaylistPanel::NoteSpot> PlaylistPanel::spotAt(juce::Point<int> point) const
{
    if (!notesGrabbable() || !gridArea().contains(point))
        return std::nullopt;

    const auto lane = laneAtY(point.getY());
    if (lane < 0 || lane >= freeLaneCount())
        return std::nullopt;

    const auto hit = itemAt(point);
    if (!hit.has_value() || hit->audio)
        return std::nullopt;

    const auto placement = shownPlacement(domain::PlacementId::parse(hit->id).value());
    const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
    if (pattern == nullptr)
        return std::nullopt;

    // The name strip is the block's: it moves, selects and frames it.
    const auto block = bounds(*hit);
    if (point.getY() <
        block.getY() + tokens_.integer("metric.playlist.labelHeight") + tokens_.integer("stroke.hairline"))
        return std::nullopt;

    for (const auto& area : bandAreas(lane))
    {
        if (point.getY() < area.area.getY() || point.getY() >= area.area.getBottom())
            continue;

        // A track the pattern has no row for: writing there opens the row
        // (S19; in S18 the rack had to add it first).
        const auto* clip = pattern->findClipForTrack(area.band.track);

        NoteSpot spot{};
        spot.placement = placement->id;
        spot.pattern = pattern->id;
        spot.clip = clip != nullptr ? clip->id : domain::ClipId{};
        spot.lane = lane;
        spot.blockStart = placement->startBeats;
        spot.patternLength = pattern->lengthBeats;
        spot.beats = beatAtX(point.getX()) - placement->startBeats;
        spot.band = area;
        if (spot.beats < 0.0 || spot.beats >= spot.patternLength)
            return std::nullopt;

        if (const auto* row = bands_.notes(pattern->id, area.band.track); row != nullptr)
        {
            const auto [first, last] = row->within(spot.beats, spot.beats + stepBeats);
            for (auto index = first; index < last; ++index)
            {
                const auto& note = row->notes[index];
                const auto rect = noteRect(area, spot.blockStart, spot.patternLength, note);
                if (!rect.contains(point))
                    continue;
                spot.note = note;
                spot.grip = point.getX() >= rect.getRight() - tokens_.integer("metric.pianoRoll.resizeGrip");
            }
        }

        if (!spot.note.has_value())
        {
            const auto handle = tokens_.integer("metric.canvas.bandHandle");
            if (point.getY() - area.area.getY() < handle)
                spot.edge = 1;
            else if (area.area.getBottom() - point.getY() <= handle)
                spot.edge = -1;
        }
        return spot;
    }
    return std::nullopt;
}

std::optional<juce::Rectangle<int>> PlaylistPanel::noteBoundsIn(domain::PlacementId placementId,
                                                                domain::NoteId noteId) const
{
    const auto placement = shownPlacement(placementId);
    const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
    if (pattern == nullptr)
        return std::nullopt;
    const auto lane = shownLaneIndex(placement->laneId);
    if (!lane)
        return std::nullopt;

    for (const auto& area : bandAreas(static_cast<int>(lane.value())))
    {
        const auto* clip = pattern->findClipForTrack(area.band.track);
        if (clip == nullptr)
            continue;
        for (const auto& note : clip->notes)
        {
            if (note.id == noteId)
                return noteRect(area, placement->startBeats, pattern->lengthBeats, note);
        }
    }
    return std::nullopt;
}

std::optional<juce::Point<int>> PlaylistPanel::notePointIn(domain::PlacementId placementId,
                                                           domain::TrackId track,
                                                           double beats,
                                                           int pitch) const
{
    const auto placement = shownPlacement(placementId);
    if (!placement.has_value())
        return std::nullopt;
    const auto lane = shownLaneIndex(placement->laneId);
    if (!lane)
        return std::nullopt;

    for (const auto& area : bandAreas(static_cast<int>(lane.value())))
    {
        if (area.band.track != track || pitch < area.band.low || pitch > area.band.high)
            continue;
        const auto y =
            area.area.getY() +
            static_cast<int>(std::lround((static_cast<double>(area.band.high - pitch) + 0.5) * area.row));
        const auto x = xForBeat(placement->startBeats + beats + stepBeats / 2.0);
        return juce::Point<int>{x, y};
    }
    return std::nullopt;
}

// --- painting ------------------------------------------------------------------

void PlaylistPanel::paintCanvasBlock(juce::Graphics& g,
                                     const domain::Placement& placement,
                                     const domain::Pattern& pattern,
                                     juce::Rectangle<int> block,
                                     int lane,
                                     juce::Rectangle<int> grid) const
{
    const auto hairline = tokens_.integer("stroke.hairline");
    auto inside = block;
    inside.removeFromTop(tokens_.integer("metric.playlist.labelHeight") + hairline);
    const auto content = inside.getIntersection(grid);
    const auto areas = bandAreas(lane);

    if (!content.isEmpty() && !areas.empty())
    {
        // Under a pixel a row is a smear: the S11 picture says more there.
        if (areas.front().row < 1.0)
        {
            if (const auto* preview = previews_.find(pattern.id); preview != nullptr)
                paintPreview(g, *preview, inside.reduced(tokens_.integer("space.xs"), 0));
        }
        else
        {
            const auto near = static_cast<float>(canvas::approach(canvasRow(), canvasScale()));
            const auto* shown = patternEditing::current(state_, selection_);
            const auto lit = shown != nullptr && shown->id == pattern.id;

            // Every block of a pattern on a line looks the same at a zoom: it
            // is drawn once into an image and laid as many times as it is
            // laid. The image goes when the content changes, or the size;
            // never when the view only moves (S11's rule, kept).
            const auto pixels = static_cast<std::int64_t>(inside.getWidth()) * inside.getHeight();
            if (pixels <= canvasImagePixels)
            {
                g.drawImageAt(canvasBlockImage(placement, pattern, inside, areas, near, lit),
                              inside.getX(),
                              inside.getY());
            }
            else
            {
                g.saveState();
                g.reduceClipRegion(content);
                renderCanvasBlock(g, placement, pattern, inside, content, areas, near, lit);
                g.restoreState();
            }

            // A stroke being drawn: the stems as the hand puts them, over the
            // picture, in every block of the pattern.
            if (velocityStroke_.has_value() && velocityStroke_->pattern == pattern.id)
            {
                for (const auto& area : areas)
                {
                    if (!area.velocity.isEmpty())
                        paintVelocityStrip(g, placement, pattern, area, content, true);
                }
            }

            paintAddBand(g, lane, block, content);

            // The zone of a generation, and its grey notes.
            paintBandGeneration(g, placement, pattern, areas, content);

            // A take being recorded, its notes not in the project yet.
            paintTake(g, placement, pattern, areas, content);

            // What the hand picked or points at, over the picture.
            g.saveState();
            g.reduceClipRegion(content);
            for (const auto& area : areas)
            {
                const auto* clip = pattern.findClipForTrack(area.band.track);
                if (clip == nullptr)
                    continue;
                for (const auto& note : clip->notes)
                {
                    const auto picked = isPickedNote(note.id);
                    const auto pointed = note.id == hoveredNote_ && placement.id == hoveredPlacement_;
                    if (!picked && !pointed)
                        continue;
                    const auto rect = noteRect(area, placement.startBeats, pattern.lengthBeats, note);
                    // Picked: filled, as the piano roll fills them; an
                    // outline alone is lost on a lit block.
                    if (picked)
                    {
                        g.setColour(tokens_.colour("color.note.selected"));
                        g.fillRect(rect);
                    }
                    if (pointed)
                    {
                        g.setColour(tokens_.colour("color.accent.live"));
                        g.drawRect(rect, hairline);
                    }
                }
            }
            g.restoreState();
        }
    }

    // The same pattern, elsewhere: it changes with the one under the hand.
    if (!hoveredPattern_.isNil() && pattern.id == hoveredPattern_)
    {
        const auto radius = tokens_.number("radius.sm");
        if (placement.id != hoveredPlacement_)
        {
            g.setColour(tokens_.colour("color.state.selected"));
            g.fillRoundedRectangle(block.toFloat(), radius);
            g.setColour(tokens_.colour("color.accent.primary"));
            g.drawRoundedRectangle(block.toFloat(), radius, static_cast<float>(hairline * 2));
        }
        else
        {
            const auto count =
                std::count_if(state_.arrangement().begin(),
                              state_.arrangement().end(),
                              [&](const domain::Placement& other) { return other.patternId == pattern.id; });
            if (count > 1)
            {
                g.setColour(tokens_.colour("color.note.label"));
                g.setFont(lookAndFeel_.typography().mono("font.size.micro", "font.weight.regular"));
                g.drawText(juce::String::fromUTF8(u8"×") + juce::String{static_cast<int>(count)},
                           block.reduced(tokens_.integer("space.xs"), 0)
                               .removeFromTop(tokens_.integer("metric.playlist.labelHeight") + hairline),
                           juce::Justification::centredRight,
                           false);
            }
        }
    }
}

const juce::Image& PlaylistPanel::canvasBlockImage(const domain::Placement& placement,
                                                   const domain::Pattern& pattern,
                                                   juce::Rectangle<int> inside,
                                                   const std::vector<BandArea>& areas,
                                                   float near,
                                                   bool lit) const
{
    if (imagesVersion_ != version_)
    {
        blockImages_.clear();
        imagesVersion_ = version_;
    }

    // A zoom gliding through its steps would draw an image per step: the
    // fade is kept to sixteen shades.
    const auto shade = static_cast<int>(std::lround(near * 16.0f));
    const auto key = pattern.id.toString() + "|" + placement.laneId.toString() + "|" +
                     std::to_string(inside.getWidth()) + "x" + std::to_string(inside.getHeight()) + "|" +
                     std::to_string(shade) + (lit ? "|lit" : "") + "|" + std::to_string(beatWidth());
    if (const auto found = blockImages_.find(key); found != blockImages_.end())
        return found->second;

    // Too many zooms kept: start again rather than grow without end.
    if (blockImages_.size() >= canvasImageCount)
        blockImages_.clear();

    juce::Image image{
        juce::Image::ARGB, std::max(1, inside.getWidth()), std::max(1, inside.getHeight()), true};
    {
        juce::Graphics drawn{image};
        drawn.setOrigin(-inside.getPosition());
        renderCanvasBlock(
            drawn, placement, pattern, inside, inside, areas, static_cast<float>(shade) / 16.0f, lit);
    }
    ++imageBuilds_;
    return blockImages_.emplace(key, std::move(image)).first->second;
}

void PlaylistPanel::renderCanvasBlock(juce::Graphics& g,
                                      const domain::Placement& placement,
                                      const domain::Pattern& pattern,
                                      juce::Rectangle<int> inside,
                                      juce::Rectangle<int> content,
                                      const std::vector<BandArea>& areas,
                                      float near,
                                      bool lit) const
{
    const auto hairline = tokens_.integer("stroke.hairline");

    // The block becomes a piano roll: its colour gives way to the grid's as
    // the zoom nears the notes.
    g.setColour(tokens_.colour(lit ? "color.note.fill" : "color.note.fillSoft")
                    .interpolatedWith(tokens_.colour("color.grid.rowWhite"), near));
    g.fillRect(content);

    const auto from = beatAtX(content.getX()) - placement.startBeats;
    const auto to = beatAtX(content.getRight()) - placement.startBeats;
    const auto stepPixels = beatWidth() * stepBeats;
    const auto noteColour =
        tokens_.colour("color.preview.note").interpolatedWith(tokens_.colour("color.note.fill"), near);
    juce::ignoreUnused(inside);

    for (const auto& area : areas)
    {
        const auto band =
            area.area.withX(content.getX()).withWidth(content.getWidth()).getIntersection(content);
        if (band.isEmpty())
            continue;

        if (near > 0.0f && area.row >= tokens_.number("metric.canvas.rowShadeMin"))
        {
            g.setColour(tokens_.colour("color.grid.rowBlack").withMultipliedAlpha(near));
            for (auto pitch = area.band.low; pitch <= area.band.high; ++pitch)
            {
                if (!isBlackKey(pitch))
                    continue;
                const auto fromTop = static_cast<double>(area.band.high - pitch);
                const auto top = area.area.getY() + static_cast<int>(std::lround(fromTop * area.row));
                const auto bottom =
                    area.area.getY() + static_cast<int>(std::lround((fromTop + 1.0) * area.row));
                g.fillRect(band.getX(), top, band.getWidth(), bottom - top);
            }
        }

        if (near > 0.0f && stepPixels >= tokens_.number("metric.canvas.stepLineMin"))
        {
            const auto first = static_cast<int>(std::floor(std::max(0.0, from) / stepBeats));
            const auto last = static_cast<int>(std::ceil(std::min(pattern.lengthBeats, to) / stepBeats));
            for (auto step = first; step <= last; ++step)
            {
                g.setColour(tokens_.colour(step % 4 == 0 ? "color.grid.beat" : "color.grid.subdivision")
                                .withMultipliedAlpha(near));
                g.fillRect(xForBeat(placement.startBeats + step * stepBeats),
                           band.getY(),
                           hairline,
                           band.getHeight());
            }
        }

        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(band.getX(), area.area.getBottom() - hairline, band.getWidth(), hairline);

        const auto* row = bands_.notes(pattern.id, area.band.track);
        if (row == nullptr)
            continue;

        g.setColour(noteColour);
        const auto [firstNote, lastNote] = row->within(std::max(0.0, from), to);
        for (auto index = firstNote; index < lastNote; ++index)
        {
            const auto& note = row->notes[index];
            if (note.pitch < area.band.low || note.pitch > area.band.high ||
                note.startBeats >= pattern.lengthBeats)
                continue;
            // Softer notes are paler, as in the piano roll.
            const auto amount =
                static_cast<float>(note.velocity - domain::Note::lowestVelocity) /
                static_cast<float>(domain::Note::highestVelocity - domain::Note::lowestVelocity);
            g.setColour(noteColour.withMultipliedAlpha(0.45f + 0.55f * amount));
            g.fillRect(noteRect(area, placement.startBeats, pattern.lengthBeats, note));
        }

        if (!area.velocity.isEmpty())
            paintVelocityStrip(g, placement, pattern, area, content, false);
    }
}

void PlaylistPanel::paintTake(juce::Graphics& g,
                              const domain::Placement& placement,
                              const domain::Pattern& pattern,
                              const std::vector<BandArea>& areas,
                              juce::Rectangle<int> content) const
{
    // In pattern mode the take is written into the auditioned pattern: drawn
    // in each of its blocks. In song mode it becomes a pattern of its own at
    // the end, which no block shows yet: the transport counts its notes.
    if (live_.recording() != LiveHost::Recording::recording || live_.takeInSong() ||
        live_.takePattern() != pattern.id)
        return;

    g.saveState();
    g.reduceClipRegion(content);
    for (const auto& played : live_.takeNotes())
    {
        for (const auto& area : areas)
        {
            if (area.band.track != played.track || played.pitch < area.band.low ||
                played.pitch > area.band.high)
                continue;
            domain::Note shown{};
            shown.pitch = played.pitch;
            shown.startBeats = played.startBeats;
            shown.lengthBeats = played.lengthBeats;
            const auto rect = noteRect(area, placement.startBeats, pattern.lengthBeats, shown);
            g.setColour(tokens_.colour("color.note.take"));
            g.fillRect(rect);
            g.setColour(tokens_.colour("color.note.takeOutline"));
            g.drawRect(rect, tokens_.integer("stroke.hairline"));
        }
    }
    g.restoreState();
}

void PlaylistPanel::paintBandNames(juce::Graphics& g, int lane, juce::Rectangle<int> name) const
{
    const auto hairline = tokens_.integer("stroke.hairline");
    const auto tallEnough = tokens_.integer("metric.playlist.labelHeight");
    g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));

    for (const auto& area : bandAreas(lane))
    {
        const auto row =
            juce::Rectangle<int>{name.getX(), area.area.getY(), name.getWidth(), area.area.getHeight()};
        g.setColour(tokens_.colour("color.border.hairline"));
        g.fillRect(
            row.getX() + tokens_.integer("space.sm"), row.getBottom() - hairline, row.getWidth(), hairline);

        const auto* track = state_.findTrack(area.band.track);
        if (track == nullptr || row.getHeight() < tallEnough)
            continue;
        g.setColour(tokens_.colour("color.text.tertiary"));
        g.drawText(juce::String::fromUTF8(track->name.c_str()),
                   row.reduced(tokens_.integer("space.sm"), 0).withTrimmedLeft(tokens_.integer("space.sm")),
                   juce::Justification::centredLeft,
                   true);
    }
}

// --- the hand ------------------------------------------------------------------

void PlaylistPanel::hover(std::optional<NoteSpot> spot)
{
    const auto pattern = spot.has_value() ? spot->pattern : domain::PatternId{};
    const auto placement = spot.has_value() ? spot->placement : domain::PlacementId{};
    const auto note = spot.has_value() && spot->note.has_value() ? spot->note->id : domain::NoteId{};

    auto shape = juce::MouseCursor::NormalCursor;
    if (spot.has_value())
    {
        if (spot->grip)
            shape = juce::MouseCursor::LeftRightResizeCursor;
        else if (spot->edge != 0)
            shape = juce::MouseCursor::UpDownResizeCursor;
        else if (!spot->note.has_value())
            shape = juce::MouseCursor::CrosshairCursor;
    }
    setMouseCursor(shape);

    if (pattern == hoveredPattern_ && placement == hoveredPlacement_ && note == hoveredNote_)
        return;
    hoveredPattern_ = pattern;
    hoveredPlacement_ = placement;
    hoveredNote_ = note;
    repaint();
}

void PlaylistPanel::mouseMove(const juce::MouseEvent& event)
{
    pointer_ = event.getPosition();
    if (!canvas_ || noteDrag_.has_value() || edgeDrag_.has_value() || pan_.has_value())
        return;
    catchUp();
    hover(spotAt(event.getPosition()));
}

void PlaylistPanel::mouseExit(const juce::MouseEvent& event)
{
    juce::ignoreUnused(event);
    if (canvas_ && !noteDrag_.has_value())
        hover(std::nullopt);
}

bool PlaylistPanel::canvasMouseDown(const juce::MouseEvent& event)
{
    if (!canvas_ || event.mods.isAltDown())
        return false;

    catchUp();
    closeVelocityWheel();
    if (addBandMouseDown(event))
        return true;
    if (bandRangeMouseDown(event) || velocityMouseDown(event))
        return true;
    const auto spot = spotAt(event.getPosition());
    if (!spot.has_value())
        return false;

    // Touching a note chooses its pattern and its channel, as a click in the
    // rack would: the rack and the piano roll follow.
    selection_.selectPattern(spot->pattern);
    selection_.selectTrack(spot->band.band.track);
    selected_.clear();

    if (event.mods.isRightButtonDown())
    {
        if (spot->note.has_value())
            static_cast<void>(bus_.execute(std::make_unique<domain::RemoveNote>(spot->clip, spot->note->id)));
        return true;
    }

    if (spot->edge != 0 && !spot->note.has_value())
    {
        const auto laneId = lineId(spot->lane);
        EdgeDrag drag{};
        drag.key = keyOf(laneId, spot->band.band.track);
        drag.top = spot->edge > 0;
        drag.grabY = event.getPosition().getY();
        if (const auto found = extensions_.find(drag.key); found != extensions_.end())
            drag.start = found->second;
        drag.row = spot->band.row;
        edgeDrag_ = drag;
        return true;
    }

    if (event.mods.isCtrlDown())
    {
        if (spot->note.has_value())
        {
            const auto id = spot->note->id;
            if (isPickedNote(id))
                pickedNotes_.erase(std::remove_if(pickedNotes_.begin(),
                                                  pickedNotes_.end(),
                                                  [id](const PickedNote& picked)
                                                  { return picked.note == id; }),
                                   pickedNotes_.end());
            else
                pickedNotes_.push_back({spot->placement, spot->clip, id});
            repaint();
            return true;
        }

        // Ctrl on the grid of a block, where no note is: a band that
        // catches notes, across blocks and lines (S19).
        bandPicksNotes_ = true;
        bandStart_ = event.getPosition();
        band_ = juce::Rectangle<int>{bandStart_, bandStart_};
        if (!event.mods.isShiftDown())
            pickedNotes_.clear();
        repaint();
        return true;
    }

    if (spot->note.has_value())
    {
        if (!isPickedNote(spot->note->id))
            pickedNotes_ = {{spot->placement, spot->clip, spot->note->id}};

        NoteDrag drag{};
        drag.clip = spot->clip;
        drag.note = spot->note->id;
        drag.placement = spot->placement;
        drag.lane = spot->lane;
        drag.track = spot->band.band.track;
        drag.grabOffset = spot->beats - spot->note->startBeats;
        drag.patternLength = spot->patternLength;
        drag.resize = spot->grip;
        // Plain char and not u8: a gesture label is a std::string_view for
        // the domain, which carries UTF-8 bytes and never decodes them.
        drag.gesture = bus_.beginGesture(drag.resize ? "allonger une note" : "déplacer une note");
        noteDrag_ = drag;

        frozen_ = std::make_pair(lineId(spot->lane), bandsOfLane(spot->lane));
        ++version_;
        repaint();
        return true;
    }

    // The second click of a double-click writes nothing more.
    if (event.getNumberOfClicks() > 1)
        return true;

    domain::Note note{};
    note.id = domain::NoteId::generate();
    note.pitch = pitchAt(spot->band, event.getPosition().getY());
    note.velocity = defaultVelocity;
    note.startBeats = std::clamp(
        std::floor(spot->beats / stepBeats) * stepBeats, 0.0, std::max(0.0, spot->patternLength - stepBeats));
    note.lengthBeats = stepBeats;
    // A channel the pattern has no row for: the row is
    // opened and the note written, one entry, as the rack's first step does.
    if (spot->clip.isNil())
    {
        auto row = patternEditing::rowFor(state_, spot->pattern, spot->band.band.track);
        auto commands = std::move(row.opening);
        commands.push_back(std::make_unique<domain::AddNote>(row.clipId, note));
        domain::GroupOptions group{};
        group.label = "écrire une note";
        if (bus_.executeGroup(std::move(commands), group).ok())
            pickedNotes_ = {{spot->placement, row.clipId, note.id}};
        repaint();
        return true;
    }

    if (bus_.execute(std::make_unique<domain::AddNote>(spot->clip, note)).ok())
        pickedNotes_ = {{spot->placement, spot->clip, note.id}};
    repaint();
    return true;
}

void PlaylistPanel::pickNotesIn(juce::Rectangle<int> area)
{
    // Every note drawn in the band, in every block it crosses. A note of a
    // pattern laid twice is one note: caught through the first block that
    // shows it, and lit in both.
    for (const auto& block : content().blocks)
    {
        if (block.clip.has_value() || !blockBounds(block, 0.0, 0).intersects(area))
            continue;
        const auto placementId = domain::PlacementId::parse(block.item.id);
        const auto placement = placementId ? shownPlacement(placementId.value()) : std::nullopt;
        const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
        if (pattern == nullptr)
            continue;

        for (const auto& bandArea : bandAreas(block.lane))
        {
            if (!bandArea.area.intersects(area))
                continue;
            const auto* clip = pattern->findClipForTrack(bandArea.band.track);
            if (clip == nullptr)
                continue;
            for (const auto& note : clip->notes)
            {
                if (isPickedNote(note.id) ||
                    !noteRect(bandArea, placement->startBeats, pattern->lengthBeats, note).intersects(area))
                    continue;
                pickedNotes_.push_back({placement->id, clip->id, note.id});
            }
        }
    }
}

bool PlaylistPanel::canvasMouseDrag(const juce::MouseEvent& event)
{
    if (bandRangeMouseDrag(event))
        return true;
    if (velocityStroke_.has_value())
    {
        strokeVelocity(event.getPosition());
        return true;
    }

    if (edgeDrag_.has_value())
    {
        const auto rows =
            static_cast<int>(std::lround(static_cast<double>(event.getPosition().getY() - edgeDrag_->grabY) /
                                         std::max(1.0, edgeDrag_->row)));
        auto extension = edgeDrag_->start;
        if (edgeDrag_->top)
            extension.above = std::max(0, edgeDrag_->start.above - rows);
        else
            extension.below = std::max(0, edgeDrag_->start.below + rows);

        if (extensions_[edgeDrag_->key] != extension)
        {
            extensions_[edgeDrag_->key] = extension;
            ++version_;
            updateScrollBars();
            repaint();
        }
        return true;
    }

    if (!noteDrag_.has_value())
        return false;

    const auto placement = shownPlacement(noteDrag_->placement);
    const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
    const auto* clip = pattern != nullptr ? pattern->findClipForTrack(noteDrag_->track) : nullptr;
    if (clip == nullptr)
        return true;
    const auto found = std::find_if(clip->notes.begin(),
                                    clip->notes.end(),
                                    [this](const domain::Note& note) { return note.id == noteDrag_->note; });
    if (found == clip->notes.end())
        return true;

    const auto areas = bandAreas(noteDrag_->lane);
    const auto area =
        std::find_if(areas.begin(),
                     areas.end(),
                     [this](const BandArea& candidate) { return candidate.band.track == noteDrag_->track; });
    if (area == areas.end())
        return true;

    domain::ExecuteOptions options{};
    options.gesture = noteDrag_->gesture;
    const auto beats = beatAtX(event.getPosition().getX()) - placement->startBeats;

    if (noteDrag_->resize)
    {
        // The right edge follows the hand, a sixteenth at least, never past
        // the end of the pattern.
        const auto edge = std::floor(beats / stepBeats) * stepBeats + stepBeats;
        const auto room = std::max(stepBeats, noteDrag_->patternLength - found->startBeats);
        const auto length = std::clamp(edge - found->startBeats, stepBeats, room);
        if (std::abs(found->lengthBeats - length) >= stepBeats / 2.0)
            static_cast<void>(bus_.execute(
                std::make_unique<domain::ResizeNote>(clip->id, noteDrag_->note, length), options));
        return true;
    }

    const auto start = std::clamp(std::floor((beats - noteDrag_->grabOffset) / stepBeats + 0.5) * stepBeats,
                                  0.0,
                                  std::max(0.0, noteDrag_->patternLength - stepBeats));
    const auto pitch = pitchAt(*area, event.getPosition().getY());

    // Nothing changed at this pixel: no command. One per step, not per move.
    if (pitch == found->pitch && std::abs(found->startBeats - start) < stepBeats / 2.0)
        return true;
    static_cast<void>(
        bus_.execute(std::make_unique<domain::MoveNote>(clip->id, noteDrag_->note, pitch, start), options));
    return true;
}

bool PlaylistPanel::canvasMouseUp()
{
    if (bandRangeMouseUp())
        return true;
    if (velocityStroke_.has_value())
    {
        commitVelocityStroke();
        return true;
    }

    if (edgeDrag_.has_value())
    {
        edgeDrag_.reset();
        return true;
    }

    if (!noteDrag_.has_value())
        return false;

    static_cast<void>(bus_.endGesture(noteDrag_->gesture));
    noteDrag_.reset();

    // The bands may breathe again: the range follows the notes once the
    // hand has let go.
    frozen_.reset();
    ++version_;
    updateScrollBars();
    repaint();
    return true;
}

bool PlaylistPanel::canvasDoubleClick(juce::Point<int> point)
{
    if (!canvas_ || !gridArea().contains(point))
        return false;

    // In a band, a double-click is two clicks: the first wrote.
    if (spotAt(point).has_value())
        return true;

    const auto hit = itemAt(point);
    if (!hit.has_value() || hit->audio)
        return false;

    frameBlock(domain::PlacementId::parse(hit->id).value());
    return true;
}

bool PlaylistPanel::canvasKey(const juce::KeyPress& key)
{
    if (!canvas_)
        return false;

    if (!pickedNotes_.empty() && (key == juce::KeyPress{juce::KeyPress::deleteKey} ||
                                  key == juce::KeyPress{juce::KeyPress::backspaceKey}))
    {
        std::vector<std::unique_ptr<domain::Command>> commands;
        for (const auto& picked : pickedNotes_)
            commands.push_back(std::make_unique<domain::RemoveNote>(picked.clip, picked.note));
        domain::GroupOptions group{};
        group.label = pickedNotes_.size() == 1 ? "retirer une note" : "retirer des notes";
        if (bus_.executeGroup(std::move(commands), group).ok())
            pickedNotes_.clear();
        return true;
    }

    const auto ctrl = juce::ModifierKeys::ctrlModifier;
    if (key == juce::KeyPress{'c', ctrl, 0} && !pickedNotes_.empty())
    {
        copyPickedNotes();
        return true;
    }
    if (key == juce::KeyPress{'b', ctrl, 0} && !pickedNotes_.empty())
    {
        duplicatePickedNotes();
        return true;
    }
    if (key == juce::KeyPress{'v', ctrl, 0} && (lastCopyWasNotes_ || clipboard_.empty()) &&
        pasteNotesUnderHand())
        return true;

    if (!pickedNotes_.empty() && key == juce::KeyPress{'q', ctrl, 0})
    {
        editPickedNotes(
            "quantifier",
            [](domain::ClipId clip, std::vector<domain::NoteId> notes) -> std::unique_ptr<domain::Command>
            { return std::make_unique<domain::QuantizeNotes>(clip, std::move(notes), stepBeats); });
        return true;
    }

    // A semitone, an octave with Ctrl: FL's arrows.
    const auto up = key.getKeyCode() == juce::KeyPress::upKey;
    if (!pickedNotes_.empty() && (up || key.getKeyCode() == juce::KeyPress::downKey) &&
        !key.getModifiers().isShiftDown() && !key.getModifiers().isAltDown())
    {
        const auto semitones = (up ? 1 : -1) * (key.getModifiers().isCtrlDown() ? 12 : 1);
        editPickedNotes(
            "transposer",
            [semitones](domain::ClipId clip,
                        std::vector<domain::NoteId> notes) -> std::unique_ptr<domain::Command>
            { return std::make_unique<domain::TransposeNotes>(clip, std::move(notes), semitones); });
        return true;
    }

    // Escape lets go of the picked notes, and only that: the whole song is
    // Shift+F, as everywhere (S18).
    if (key == juce::KeyPress{juce::KeyPress::escapeKey} && !pickedNotes_.empty())
    {
        pickedNotes_.clear();
        repaint();
        return true;
    }
    return false;
}

// --- a track added to a pattern ------------------------------------------------------

std::vector<domain::TrackId> PlaylistPanel::channelsMissingFrom(const std::vector<CanvasBand>& bands) const
{
    std::vector<domain::TrackId> missing;
    for (const auto& track : state_.tracks())
    {
        if (std::none_of(bands.begin(),
                         bands.end(),
                         [&track](const CanvasBand& band) { return band.track == track.id; }))
            missing.push_back(track.id);
    }
    return missing;
}

int PlaylistPanel::addBandOf(int lane, const std::vector<CanvasBand>& bands) const
{
    if (!notesGrabbable() || patternMode() || lane < 0 || lane >= freeLaneCount() || bands.empty() ||
        channelsMissingFrom(bands).empty())
        return 0;
    return tokens_.integer("metric.canvas.addBand");
}

juce::Rectangle<int> PlaylistPanel::addBandArea(int lane) const
{
    const auto bands = bandsOfLane(lane);
    const auto height = addBandOf(lane, bands);
    const auto areas = bandAreas(lane);
    if (height == 0 || areas.empty())
        return {};
    auto bottom = 0;
    for (const auto& area : areas)
        bottom = std::max(
            {bottom, area.area.getBottom(), area.velocity.isEmpty() ? 0 : area.velocity.getBottom()});
    const auto grid = gridArea();
    return {grid.getX(), bottom, grid.getWidth(), height};
}

std::optional<juce::Rectangle<int>> PlaylistPanel::addBandIn(domain::PlacementId placementId) const
{
    const auto placement = shownPlacement(placementId);
    if (!placement.has_value())
        return std::nullopt;
    const auto lane = shownLaneIndex(placement->laneId);
    const auto* pattern = state_.findPattern(placement->patternId);
    if (!lane || pattern == nullptr)
        return std::nullopt;
    const auto area = addBandArea(*lane);
    if (area.isEmpty())
        return std::nullopt;
    return area.withLeft(xForBeat(placement->startBeats))
        .withRight(xForBeat(placement->startBeats + pattern->lengthBeats));
}

bool PlaylistPanel::addBandMouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    const auto lane = laneAtY(point.getY());
    if (lane < 0 || lane >= freeLaneCount() || !addBandArea(lane).contains(point))
        return false;
    const auto hit = itemAt(point);
    if (!hit.has_value() || hit->audio)
        return true;
    const auto placement = shownPlacement(domain::PlacementId::parse(hit->id).value());
    if (!placement.has_value())
        return true;

    const auto missing = channelsMissingFrom(bandsOfLane(lane));
    juce::PopupMenu menu;
    menu.addSectionHeader(juce::String::fromUTF8(u8"Ajouter au pattern"));
    for (std::size_t index = 0; index < missing.size(); ++index)
    {
        const auto* track = state_.findTrack(missing[index]);
        menu.addItem(static_cast<int>(index) + 1,
                     juce::String::fromUTF8(track != nullptr ? track->name.c_str() : "?"));
    }

    juce::Component::SafePointer<PlaylistPanel> safe{this};
    const auto patternId = placement->patternId;
    menu.showMenuAsync(juce::PopupMenu::Options{}.withMousePosition(),
                       [safe, missing, patternId](int chosen)
                       {
                           if (safe == nullptr || chosen < 1 || chosen > static_cast<int>(missing.size()))
                               return;
                           auto row = patternEditing::rowFor(
                               safe->state_, patternId, missing[static_cast<std::size_t>(chosen - 1)]);
                           if (row.opening.empty())
                               return;
                           domain::GroupOptions group{};
                           group.label = "ajouter une piste au pattern";
                           static_cast<void>(safe->bus_.executeGroup(std::move(row.opening), group));
                       });
    return true;
}

void PlaylistPanel::paintAddBand(juce::Graphics& g,
                                 int lane,
                                 juce::Rectangle<int> block,
                                 juce::Rectangle<int> content) const
{
    const auto area = addBandArea(lane).getIntersection(block).getIntersection(content);
    if (area.isEmpty())
        return;
    g.setColour(tokens_.colour("color.surface.raised"));
    g.fillRect(area);
    g.setColour(tokens_.colour("color.text.tertiary"));
    g.setFont(lookAndFeel_.typography().sans("font.size.micro", "font.weight.regular"));
    g.drawText(juce::String::fromUTF8(u8"+ piste"),
               area.reduced(tokens_.integer("space.xs"), 0),
               juce::Justification::centredLeft,
               false);
}

// --- velocities ------------------------------------------------------------------

int PlaylistPanel::yForVelocity(juce::Rectangle<int> strip, int velocity) const
{
    const auto handle = tokens_.integer("metric.pianoRoll.velocityHandle");
    const auto lane = strip.withTrimmedTop(handle * 2).withTrimmedBottom(handle);
    const auto amount = static_cast<double>(velocity - domain::Note::lowestVelocity) /
                        static_cast<double>(domain::Note::highestVelocity - domain::Note::lowestVelocity);
    return lane.getBottom() - static_cast<int>(std::lround(amount * lane.getHeight()));
}

int PlaylistPanel::velocityAtY(juce::Rectangle<int> strip, int y) const
{
    const auto handle = tokens_.integer("metric.pianoRoll.velocityHandle");
    const auto lane = strip.withTrimmedTop(handle * 2).withTrimmedBottom(handle);
    const auto amount = static_cast<double>(lane.getBottom() - y) / std::max(1, lane.getHeight());
    const auto range = domain::Note::highestVelocity - domain::Note::lowestVelocity;
    return std::clamp(domain::Note::lowestVelocity + static_cast<int>(std::lround(amount * range)),
                      domain::Note::lowestVelocity,
                      domain::Note::highestVelocity);
}

// As in the piano roll: with two picked notes or more in the row, only those.
bool PlaylistPanel::velocityEditable(domain::ClipId clip, domain::NoteId note) const
{
    const auto inRow = std::count_if(
        pickedNotes_.begin(), pickedNotes_.end(), [clip](const PickedNote& one) { return one.clip == clip; });
    return inRow < 2 || isPickedNote(note);
}

void PlaylistPanel::paintVelocityStrip(juce::Graphics& g,
                                       const domain::Placement& placement,
                                       const domain::Pattern& pattern,
                                       const BandArea& area,
                                       juce::Rectangle<int> clip,
                                       bool stroke) const
{
    const auto strip = area.velocity.getIntersection(clip);
    if (strip.isEmpty())
        return;
    const auto hairline = tokens_.integer("stroke.hairline");

    g.saveState();
    g.reduceClipRegion(strip);
    g.setColour(tokens_.colour("color.surface.sunken"));
    g.fillRect(strip);
    // The guide at 100, the velocity a written note gets.
    g.setColour(tokens_.colour("color.grid.beat"));
    g.fillRect(strip.getX(), yForVelocity(area.velocity, 100), strip.getWidth(), hairline);

    const auto* row = pattern.findClipForTrack(area.band.track);
    if (row != nullptr)
    {
        const auto stem = tokens_.integer("metric.pianoRoll.velocityStem");
        const auto radius = tokens_.number("metric.pianoRoll.velocityHandle");
        for (const auto& note : row->notes)
        {
            if (note.startBeats >= pattern.lengthBeats)
                continue;
            auto velocity = note.velocity;
            if (stroke && velocityStroke_.has_value())
            {
                for (const auto& [id, value] : velocityStroke_->values)
                    if (id == note.id)
                        velocity = value;
            }
            const auto x = xForBeat(placement.startBeats + note.startBeats);
            const auto top = yForVelocity(area.velocity, velocity);
            g.setColour(tokens_.colour(!velocityEditable(row->id, note.id) ? "color.note.fillSoft"
                                       : isPickedNote(note.id)             ? "color.note.selected"
                                                                           : "color.note.fill"));
            g.fillRect(x, top, stem, area.velocity.getBottom() - top);
            g.fillEllipse(juce::Rectangle<float>{radius * 2.0f, radius * 2.0f}.withCentre(juce::Point<float>{
                static_cast<float>(x) + static_cast<float>(stem) / 2.0f, static_cast<float>(top)}));
        }
    }
    g.setColour(tokens_.colour("color.border.hairline"));
    g.fillRect(strip.getX(), area.velocity.getBottom() - hairline, strip.getWidth(), hairline);
    g.restoreState();
}

std::optional<juce::Rectangle<int>> PlaylistPanel::velocityStripIn(domain::PlacementId placementId) const
{
    const auto placement = shownPlacement(placementId);
    if (!placement.has_value())
        return std::nullopt;
    const auto lane = shownLaneIndex(placement->laneId);
    if (!lane)
        return std::nullopt;
    for (const auto& area : bandAreas(static_cast<int>(lane.value())))
    {
        if (area.velocity.isEmpty())
            continue;
        const auto* pattern = state_.findPattern(placement->patternId);
        if (pattern == nullptr)
            return std::nullopt;
        const auto left = xForBeat(placement->startBeats);
        const auto right = xForBeat(placement->startBeats + pattern->lengthBeats);
        const auto grid = gridArea();
        return area.velocity.withLeft(std::max(left, grid.getX()))
            .withRight(std::min(right, grid.getRight()));
    }
    return std::nullopt;
}

bool PlaylistPanel::velocityMouseDown(const juce::MouseEvent& event)
{
    if (!notesGrabbable() || event.mods.isRightButtonDown() || event.mods.isCtrlDown())
        return false;
    const auto point = event.getPosition();
    const auto lane = laneAtY(point.getY());
    if (lane < 0 || lane >= freeLaneCount())
        return false;

    for (const auto& area : bandAreas(lane))
    {
        if (area.velocity.isEmpty() || !area.velocity.contains(point))
            continue;
        const auto hit = itemAt(point);
        if (!hit.has_value() || hit->audio)
            return true;
        const auto placement = shownPlacement(domain::PlacementId::parse(hit->id).value());
        const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
        const auto* row = pattern != nullptr ? pattern->findClipForTrack(area.band.track) : nullptr;
        if (row == nullptr)
            return true;

        VelocityStroke stroke{};
        stroke.placement = placement->id;
        stroke.pattern = pattern->id;
        stroke.clip = row->id;
        stroke.track = area.band.track;
        stroke.lane = lane;
        stroke.last = point;
        velocityStroke_ = stroke;
        strokeVelocity(point);
        return true;
    }
    return false;
}

// A stem is crossed when the line from the last point to this one passes
// over it, give or take its head: a click next to a stem still reaches it.
void PlaylistPanel::strokeVelocity(juce::Point<int> to)
{
    auto& stroke = *velocityStroke_;
    const auto placement = shownPlacement(stroke.placement);
    const auto* pattern = state_.findPattern(stroke.pattern);
    const auto* row = pattern != nullptr ? pattern->findClipForTrack(stroke.track) : nullptr;
    if (!placement.has_value() || row == nullptr)
        return;

    BandArea area{};
    for (const auto& candidate : bandAreas(stroke.lane))
        if (!candidate.velocity.isEmpty())
            area = candidate;
    if (area.velocity.isEmpty())
        return;

    const auto from = stroke.last;
    const auto reach = tokens_.integer("metric.pianoRoll.velocityHandle") * 2;
    const auto left = std::min(from.getX(), to.getX()) - reach;
    const auto right = std::max(from.getX(), to.getX()) + reach;
    for (const auto& note : row->notes)
    {
        if (!velocityEditable(row->id, note.id) || note.startBeats >= pattern->lengthBeats)
            continue;
        const auto x = xForBeat(placement->startBeats + note.startBeats);
        if (x < left || x > right)
            continue;
        const auto span = to.getX() - from.getX();
        const auto along =
            span == 0 ? 1.0 : std::clamp(static_cast<double>(x - from.getX()) / span, 0.0, 1.0);
        const auto y = static_cast<int>(std::lround(from.getY() + along * (to.getY() - from.getY())));
        const auto velocity = velocityAtY(area.velocity, y);
        const auto found = std::find_if(stroke.values.begin(),
                                        stroke.values.end(),
                                        [&note](const auto& entry) { return entry.first == note.id; });
        if (found != stroke.values.end())
            found->second = velocity;
        else
            stroke.values.emplace_back(note.id, velocity);
    }
    stroke.last = to;
    repaint();
}

void PlaylistPanel::commitVelocityStroke()
{
    const auto stroke = std::move(*velocityStroke_);
    velocityStroke_.reset();

    const auto* pattern = state_.findPattern(stroke.pattern);
    const auto* row = pattern != nullptr ? pattern->findClipForTrack(stroke.track) : nullptr;
    if (row == nullptr)
        return;

    std::vector<std::unique_ptr<domain::Command>> commands;
    for (const auto& [id, velocity] : stroke.values)
    {
        const auto found = std::find_if(row->notes.begin(),
                                        row->notes.end(),
                                        [id = id](const domain::Note& note) { return note.id == id; });
        if (found != row->notes.end() && found->velocity != velocity)
            commands.push_back(std::make_unique<domain::SetNoteVelocity>(row->id, id, velocity));
    }
    if (!commands.empty())
    {
        domain::GroupOptions group{};
        group.label = commands.size() > 1 ? "vélocités" : "vélocité";
        static_cast<void>(bus_.executeGroup(std::move(commands), group));
    }
    repaint();
}

// Alt + wheel on a note: its velocity, a few steps a notch, one entry per
// gesture (the grammar of S18: Alt + wheel tunes what is under the hand).
bool PlaylistPanel::velocityWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!canvas_ || !event.mods.isAltDown() || event.mods.isCtrlDown() || wheel.deltaY == 0.0f)
        return false;
    catchUp();
    const auto spot = spotAt(event.getPosition());
    if (!spot.has_value() || !spot->note.has_value())
        return false;

    const auto step = tokens_.integer("metric.canvas.velocityWheelStep");
    const auto velocity = std::clamp(spot->note->velocity + (wheel.deltaY > 0.0f ? step : -step),
                                     domain::Note::lowestVelocity,
                                     domain::Note::highestVelocity);
    if (velocity == spot->note->velocity)
        return true;

    lastVelocityWheelMs_ = juce::Time::getMillisecondCounter();
    if (!velocityWheelGesture_.has_value() || bus_.openGesture() != velocityWheelGesture_)
        velocityWheelGesture_ = bus_.beginGesture("vélocité à la molette");
    domain::ExecuteOptions options;
    options.gesture = velocityWheelGesture_;
    static_cast<void>(bus_.execute(
        std::make_unique<domain::SetNoteVelocity>(spot->clip, spot->note->id, velocity), options));
    return true;
}

void PlaylistPanel::closeVelocityWheel(bool onlyWhenRested)
{
    if (!velocityWheelGesture_.has_value())
        return;
    if (onlyWhenRested && juce::Time::getMillisecondCounter() - lastVelocityWheelMs_ <= velocityWheelRestMs)
        return;
    if (bus_.openGesture() == velocityWheelGesture_)
        static_cast<void>(bus_.endGesture(*velocityWheelGesture_));
    velocityWheelGesture_.reset();
}

// --- quantise, transpose -----------------------------------------------------------

// One command per row the picked notes are in, the rows of several patterns
// together: one history entry. A row the command refuses — a note pushed past
// the keyboard — refuses the whole group, and nothing moves.
void PlaylistPanel::editPickedNotes(
    const std::string& verb,
    const std::function<std::unique_ptr<domain::Command>(domain::ClipId, std::vector<domain::NoteId>)>& make)
{
    std::vector<std::pair<domain::ClipId, std::vector<domain::NoteId>>> rows;
    for (const auto& one : pickedNotes_)
    {
        auto row =
            std::find_if(rows.begin(), rows.end(), [&one](const auto& r) { return r.first == one.clip; });
        if (row == rows.end())
        {
            rows.emplace_back(one.clip, std::vector<domain::NoteId>{});
            row = std::prev(rows.end());
        }
        row->second.push_back(one.note);
    }

    std::vector<std::unique_ptr<domain::Command>> commands;
    for (auto& [clip, notes] : rows)
        commands.push_back(make(clip, std::move(notes)));

    domain::GroupOptions group{};
    group.label =
        verb + " " + std::to_string(pickedNotes_.size()) + (pickedNotes_.size() == 1 ? " note" : " notes");
    static_cast<void>(bus_.executeGroup(std::move(commands), group));
    repaint();
}

// --- copy, paste -----------------------------------------------------------------

void PlaylistPanel::copyPickedNotes()
{
    std::vector<PickedInBlock> picked;
    for (const auto& one : pickedNotes_)
    {
        const auto placement = shownPlacement(one.placement);
        const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
        if (pattern == nullptr)
            continue;
        for (const auto& row : pattern->clips)
        {
            if (row.id != one.clip)
                continue;
            for (const auto& note : row.notes)
                if (note.id == one.note)
                    picked.push_back({placement->startBeats, row.trackId, note});
        }
    }

    auto copied = copyFromBlocks(picked);
    if (copied.rows.empty())
        return;
    notesClipboard_.notes = std::move(copied);
    lastCopyWasNotes_ = true;
}

// Into the block under the hand, at the sixteenth under the hand: pasting in
// a block writes into its pattern, and every block of it shows the copy. Each
// row goes to the track it was copied from, its row opened when the pattern
// has none. What falls past the pattern's end is left out, and the history
// says how many.
bool PlaylistPanel::pasteNotesUnderHand()
{
    if (!notesClipboard_.notes.has_value() || !notesGrabbable())
        return false;

    const auto point = pointer_;
    const auto hit = gridArea().contains(point) ? itemAt(point) : std::nullopt;
    if (!hit.has_value() || hit->audio)
        return false;
    const auto placement = shownPlacement(domain::PlacementId::parse(hit->id).value());
    const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
    if (pattern == nullptr)
        return false;

    const auto at =
        std::clamp(std::floor((beatAtX(point.getX()) - placement->startBeats) / stepBeats) * stepBeats,
                   0.0,
                   std::max(0.0, pattern->lengthBeats - stepBeats));
    auto plan = planPaste(state_, pattern->id, *notesClipboard_.notes, {}, at, false);
    if (plan.commands.empty())
        return true;

    domain::GroupOptions group{};
    group.label = "coller " + std::to_string(plan.pasted.size()) + " notes";
    if (plan.skipped > 0)
        group.label += " (" + std::to_string(plan.skipped) + " hors du pattern)";
    const auto pasted = plan.pasted;
    const auto placementId = placement->id;
    const auto patternId = pattern->id;
    if (bus_.executeGroup(std::move(plan.commands), group).ok())
        pickPasted(placementId, patternId, pasted);
    return true;
}

// Right after the copy, in the same pattern, the pattern lengthened to the
// bar when it has to be: what Ctrl+B does in the piano roll. Notes picked in
// several patterns have no one "after": nothing is done.
void PlaylistPanel::duplicatePickedNotes()
{
    const auto first = pickedNotes_.front().placement;
    const auto placement = shownPlacement(first);
    if (!placement.has_value())
        return;
    for (const auto& one : pickedNotes_)
    {
        const auto other = shownPlacement(one.placement);
        if (!other.has_value() || other->patternId != placement->patternId)
            return;
    }

    // Through one block: the same pattern seen through several is the same
    // notes.
    for (auto& one : pickedNotes_)
        one.placement = first;
    copyPickedNotes();
    if (!notesClipboard_.notes.has_value())
        return;

    const auto& copied = *notesClipboard_.notes;
    const auto at = duplicateAt(copied, copied.originBeats, state_.beatsPerBar());
    auto plan = planPaste(state_, placement->patternId, copied, {}, at, true);
    if (plan.commands.empty())
        return;

    domain::GroupOptions group{};
    group.label = "dupliquer " + std::to_string(plan.pasted.size()) + " notes";
    const auto pasted = plan.pasted;
    const auto patternId = placement->patternId;
    if (bus_.executeGroup(std::move(plan.commands), group).ok())
        pickPasted(first, patternId, pasted);
}

// The copies become the picked notes: a second Ctrl+B goes on, a Ctrl+Z
// shows what it takes away.
void PlaylistPanel::pickPasted(domain::PlacementId placement,
                               domain::PatternId patternId,
                               const std::vector<domain::NoteId>& notes)
{
    pickedNotes_.clear();
    selected_.clear();
    const auto* pattern = state_.findPattern(patternId);
    if (pattern == nullptr)
        return;
    for (const auto& row : pattern->clips)
    {
        for (const auto& note : row.notes)
        {
            if (std::find(notes.begin(), notes.end(), note.id) != notes.end())
                pickedNotes_.push_back({placement, row.id, note.id});
        }
    }
    repaint();
}

bool PlaylistPanel::frameKey(const juce::KeyPress& key)
{
    if (key.getKeyCode() != 'F' && key.getKeyCode() != 'f')
        return false;

    if (key.getModifiers().isShiftDown())
    {
        showWholeSong();
        return true;
    }

    // The selected blocks, their lines from the first.
    if (!selected_.empty())
    {
        auto from = 0.0;
        auto to = 0.0;
        auto lane = laneCount();
        auto first = true;
        for (const auto& item : selected_)
        {
            const auto start = startOf(item);
            if (!start.has_value())
                continue;
            from = first ? *start : std::min(from, *start);
            to = first ? *start + lengthOf(item) : std::max(to, *start + lengthOf(item));
            lane = std::min(lane, std::max(0, laneOf(item)));
            first = false;
        }
        if (!first)
        {
            const auto pixels = gridArea().getWidth();
            const framing::Span span{from, to, 60, 60};
            const auto width = framing::beatWidthFor(span, pixels, fitBeatWidth(), widestBeatWidth());
            setView(framing::firstBeatFor(span, width, pixels), width);
            setFirstLanePixel(laneTop(lane));
            return true;
        }
    }

    // On the canvas, the block under the hand.
    if (canvas_ && !hoveredPlacement_.isNil())
    {
        frameBlock(hoveredPlacement_);
        return true;
    }

    showWholeSong();
    return true;
}

// --- the view ------------------------------------------------------------------

void PlaylistPanel::frameBlock(domain::PlacementId placementId)
{
    const auto placement = shownPlacement(placementId);
    const auto* pattern = placement.has_value() ? state_.findPattern(placement->patternId) : nullptr;
    if (pattern == nullptr || pattern->lengthBeats <= 0.0)
        return;
    const auto lane = shownLaneIndex(placement->laneId);
    if (!lane)
        return;

    selection_.selectPattern(pattern->id);

    // At least the scale of notes, a little over it so the rounding cannot
    // leave the view one hair short; the whole block when it fits wider.
    const auto grid = gridArea();
    const auto width = static_cast<double>(std::max(1, grid.getWidth()));
    const auto scale = canvasScale();
    const auto notes = 4.0 * scale.grabStep * 1.001;
    const auto wanted = std::clamp(std::max(notes, width * (1.0 - 2.0 * frameMargin) / pattern->lengthBeats),
                                   fitBeatWidth(),
                                   widestBeatWidth());
    setView(placement->startBeats - width * frameMargin / wanted, wanted);
    setFirstLanePixel(laneTop(static_cast<int>(lane.value())));
}

void PlaylistPanel::showWholeSong()
{
    setView(0.0, std::nullopt);
    setFirstLanePixel(0);
}

} // namespace daw::ui
