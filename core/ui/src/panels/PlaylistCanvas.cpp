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
//   Delete                       removes the picked notes
//   drag the edge of a band      adds rows above or below, for this screen
//   double-click the name strip  frames the block; Escape shows the song
// The name strip keeps the block's own gestures: move it, select it, remove it.
//
// A note belongs to the pattern, not to the block: while the hand is on a
// block, the other blocks of its pattern are lit, and they change with it.
//
// Nothing here is state of the project: the zoom, the rows added by hand and
// the picked notes are the screen's (S9 §6.4). Every edit leaves as a command.

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/ui/model/PatternEditing.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
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
    return canvas::lineHeight(canvas::rowCount(bands), canvasRow(), canvasChrome(), base);
}

std::vector<CanvasBand> PlaylistPanel::bandsOfLane(int lane) const
{
    if (lane < 0 || lane >= freeLaneCount())
        return {};

    const auto laneId = state_.lanes()[static_cast<std::size_t>(lane)].id;
    if (frozen_.has_value() && frozen_->first == laneId)
        return frozen_->second;

    auto bands = bands_.of(laneId);
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
    const auto chrome = canvasChrome();
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
        areas.push_back(BandArea{band, {grid.getX(), top, grid.getWidth(), std::max(1, bottom - top)}, row});
        y = next;
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
                                      [&](const auto& picked)
                                      { return !exists(picked.first, picked.second); }),
                       pickedNotes_.end());

    if (!hoveredPlacement_.isNil() && state_.findPlacement(hoveredPlacement_) == nullptr)
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
                       [note](const auto& picked) { return picked.second == note; });
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

    const auto* placement = state_.findPlacement(domain::PlacementId::parse(hit->id).value());
    const auto* pattern = placement != nullptr ? state_.findPattern(placement->patternId) : nullptr;
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

        // A track the pattern has no row for: nothing of this block to edit
        // here; the rack adds the row.
        const auto* clip = pattern->findClipForTrack(area.band.track);
        if (clip == nullptr)
            return std::nullopt;

        NoteSpot spot{};
        spot.placement = placement->id;
        spot.pattern = pattern->id;
        spot.clip = clip->id;
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
    const auto* placement = state_.findPlacement(placementId);
    const auto* pattern = placement != nullptr ? state_.findPattern(placement->patternId) : nullptr;
    if (pattern == nullptr)
        return std::nullopt;
    const auto lane = state_.laneIndex(placement->laneId);
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
    const auto* placement = state_.findPlacement(placementId);
    if (placement == nullptr)
        return std::nullopt;
    const auto lane = state_.laneIndex(placement->laneId);
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
            const auto scale = canvasScale();
            const auto near = static_cast<float>(canvas::approach(canvasRow(), scale));
            const auto* shown = patternEditing::current(state_, selection_);
            const auto lit = shown != nullptr && shown->id == pattern.id;

            g.saveState();
            g.reduceClipRegion(content);

            // The block becomes a piano roll: its colour gives way to the
            // grid's as the zoom nears the notes.
            g.setColour(tokens_.colour(lit ? "color.note.fill" : "color.note.fillSoft")
                            .interpolatedWith(tokens_.colour("color.grid.rowWhite"), near));
            g.fillRect(content);

            const auto from = beatAtX(content.getX()) - placement.startBeats;
            const auto to = beatAtX(content.getRight()) - placement.startBeats;
            const auto stepPixels = beatWidth() * stepBeats;
            const auto noteColour = tokens_.colour("color.preview.note")
                                        .interpolatedWith(tokens_.colour("color.note.fill"), near);

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
                    const auto last =
                        static_cast<int>(std::ceil(std::min(pattern.lengthBeats, to) / stepBeats));
                    for (auto step = first; step <= last; ++step)
                    {
                        g.setColour(
                            tokens_.colour(step % 4 == 0 ? "color.grid.beat" : "color.grid.subdivision")
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

                const auto [firstNote, lastNote] = row->within(std::max(0.0, from), to);
                for (auto index = firstNote; index < lastNote; ++index)
                {
                    const auto& note = row->notes[index];
                    if (note.pitch < area.band.low || note.pitch > area.band.high ||
                        note.startBeats >= pattern.lengthBeats)
                        continue;

                    const auto rect = noteRect(area, placement.startBeats, pattern.lengthBeats, note);
                    g.setColour(noteColour);
                    g.fillRect(rect);

                    if (isPickedNote(note.id))
                    {
                        g.setColour(tokens_.colour("color.note.selected"));
                        g.drawRect(rect, hairline);
                    }
                    else if (note.id == hoveredNote_ && placement.id == hoveredPlacement_)
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
        const auto laneId = state_.lanes()[static_cast<std::size_t>(spot->lane)].id;
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
                                                  [id](const auto& picked) { return picked.second == id; }),
                                   pickedNotes_.end());
            else
                pickedNotes_.emplace_back(spot->clip, id);
            repaint();
        }
        return true;
    }

    if (spot->note.has_value())
    {
        if (!isPickedNote(spot->note->id))
            pickedNotes_ = {{spot->clip, spot->note->id}};

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

        frozen_ =
            std::make_pair(state_.lanes()[static_cast<std::size_t>(spot->lane)].id, bandsOfLane(spot->lane));
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
    if (bus_.execute(std::make_unique<domain::AddNote>(spot->clip, note)).ok())
        pickedNotes_ = {{spot->clip, note.id}};
    repaint();
    return true;
}

bool PlaylistPanel::canvasMouseDrag(const juce::MouseEvent& event)
{
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

    const auto* placement = state_.findPlacement(noteDrag_->placement);
    const auto* pattern = placement != nullptr ? state_.findPattern(placement->patternId) : nullptr;
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
        for (const auto& [clip, note] : pickedNotes_)
            commands.push_back(std::make_unique<domain::RemoveNote>(clip, note));
        domain::GroupOptions group{};
        group.label = pickedNotes_.size() == 1 ? "retirer une note" : "retirer des notes";
        if (bus_.executeGroup(std::move(commands), group).ok())
            pickedNotes_.clear();
        return true;
    }

    if (key == juce::KeyPress{juce::KeyPress::escapeKey})
    {
        if (!pickedNotes_.empty())
        {
            pickedNotes_.clear();
            repaint();
            return true;
        }
        if (zoom_.has_value() || firstLanePixel() != 0)
        {
            showWholeSong();
            return true;
        }
    }
    return false;
}

// --- the view ------------------------------------------------------------------

void PlaylistPanel::frameBlock(domain::PlacementId placementId)
{
    const auto* placement = state_.findPlacement(placementId);
    const auto* pattern = placement != nullptr ? state_.findPattern(placement->patternId) : nullptr;
    if (pattern == nullptr || pattern->lengthBeats <= 0.0)
        return;
    const auto lane = state_.laneIndex(placement->laneId);
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
