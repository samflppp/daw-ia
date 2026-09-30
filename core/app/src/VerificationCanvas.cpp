#include "Verification.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace daw::app
{
namespace
{

// The pointer passing over a point, no button held: what lights the other
// blocks of a pattern before anything is clicked.
void hoverAt(juce::Component& target, juce::Point<int> at)
{
    const auto now = juce::Time::getCurrentTime();
    const auto position = at.toFloat();
    const juce::MouseEvent event{juce::Desktop::getInstance().getMainMouseSource(),
                                 position,
                                 juce::ModifierKeys{},
                                 juce::MouseInputSource::defaultPressure,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 0.0f,
                                 &target,
                                 &target,
                                 now,
                                 position,
                                 now,
                                 0,
                                 false};
    target.mouseMove(event);
}

// The median of the times a full paint of the panel takes, drawn into an
// image the size of the panel: what the screen would cost, without the
// screen's own vsync in the number.
double medianPaintMs(juce::Component& panel, int times, const std::function<void(int)>& between = {})
{
    juce::Image image{juce::Image::ARGB, std::max(1, panel.getWidth()), std::max(1, panel.getHeight()), true};
    std::vector<double> spent;
    for (int time = 0; time < times; ++time)
    {
        if (between)
            between(time);
        juce::Graphics g{image};
        const auto started = juce::Time::getMillisecondCounterHiRes();
        panel.paintEntireComponent(g, false);
        spent.push_back(juce::Time::getMillisecondCounterHiRes() - started);
    }
    std::sort(spent.begin(), spent.end());
    return spent[spent.size() / 2];
}

std::string ms(double value)
{
    return juce::String(value, 2).toStdString() + " ms";
}

} // namespace

void Verification::addCanvasSteps()
{
    const auto canvas = [this] { return dynamic_cast<ui::PlaylistPanel*>(panel("canvas")); };

    // A pattern of three rows laid twice, on a line of its own, after the song.
    struct Laid
    {
        domain::PatternId pattern{};
        std::vector<domain::TrackId> tracks;
        std::vector<domain::ClipId> rows;
        std::vector<domain::PlacementId> placements;
        domain::NoteId written{};
        std::size_t depth{0};
        std::string before;
    };
    auto laid = std::make_shared<Laid>();

    add("S18 : F4 ouvre la toile ; dézoomée, elle montre les blocs et aucune note ne s'attrape",
        [this, canvas, laid]
        {
            if (clock_.isPlaying())
                static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            press("SONG");

            std::vector<std::unique_ptr<domain::Command>> commands;
            for (const auto* name : {"Kick toile", "808 toile", "Lead toile"})
            {
                laid->tracks.push_back(domain::TrackId::generate());
                commands.push_back(std::make_unique<domain::AddTrack>(laid->tracks.back(), name, 0.0));
            }
            laid->pattern = domain::PatternId::generate();
            commands.push_back(std::make_unique<domain::CreatePattern>(laid->pattern, "Toile", 4.0));
            for (const auto track : laid->tracks)
            {
                laid->rows.push_back(domain::ClipId::generate());
                commands.push_back(
                    std::make_unique<domain::AddPatternTrack>(laid->pattern, laid->rows.back(), track));
            }

            // A kick on every beat, an 808 on two notes, a lead of four.
            const auto note = [](int pitch, double start, double length)
            {
                domain::Note made{};
                made.id = domain::NoteId::generate();
                made.pitch = pitch;
                made.startBeats = start;
                made.lengthBeats = length;
                return made;
            };
            for (int beat = 0; beat < 4; ++beat)
                commands.push_back(std::make_unique<domain::AddNote>(laid->rows[0], note(36, beat, 0.25)));
            commands.push_back(std::make_unique<domain::AddNote>(laid->rows[1], note(34, 0.0, 2.0)));
            commands.push_back(std::make_unique<domain::AddNote>(laid->rows[1], note(41, 2.0, 2.0)));
            for (int step = 0; step < 4; ++step)
                commands.push_back(
                    std::make_unique<domain::AddNote>(laid->rows[2], note(72 + 2 * step, step * 1.0, 0.5)));

            double end = 0.0;
            for (const auto& placement : state_.arrangement())
            {
                if (const auto* pattern = state_.findPattern(placement.patternId); pattern != nullptr)
                    end = std::max(end, placement.startBeats + pattern->lengthBeats);
            }
            const auto bar = state_.beatsPerBar();
            const auto from = std::ceil(end / bar) * bar + bar;
            for (const auto start : {from, from + 8.0})
            {
                laid->placements.push_back(domain::PlacementId::generate());
                commands.push_back(
                    std::make_unique<domain::PlacePattern>(laid->placements.back(), laid->pattern, start));
            }
            domain::GroupOptions group{};
            group.label = "toile : un pattern posé deux fois";
            check(bus_.executeGroup(std::move(commands), group).ok(),
                  "le pattern de la toile est posé deux fois");

            key(juce::KeyPress{juce::KeyPress::F4Key});
            auto* view = canvas();
            check(view != nullptr && view->isShowing(), "F4 ouvre la toile");
            if (view == nullptr)
                return;
            check(view->isCanvas(), "c'est la toile, pas la playlist");
            view->showWholeSong();
            check(!view->notesGrabbable(), "dézoomée, aucune note ne s'attrape");
            snapshot("s18-toile-vue-d-ensemble");
        });

    add("S18 : double-clic sur un bloc : la toile le cadre à l'échelle des notes, trois bandes",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr || laid->placements.empty())
                return;
            const auto* placement = state_.findPlacement(laid->placements.front());
            if (placement == nullptr)
                return;
            const auto lane = state_.laneIndex(placement->laneId);
            if (!lane)
                return;

            const auto at = view->pointFor(static_cast<int>(lane.value()), placement->startBeats + 0.5);
            check(view->timelineArea().contains(at), "le bloc est à l'écran avant d'être visé");
            doubleClick(*view, at);
            note("cadré : " + juce::String(view->beatWidth(), 1).toStdString() +
                 " px par temps, premier temps " + juce::String(view->firstBeat(), 2).toStdString());
            check(view->notesGrabbable(), "cadré, les notes s'attrapent");

            auto everyRow = true;
            for (const auto track : laid->tracks)
                everyRow =
                    everyRow &&
                    view->notePointIn(placement->id,
                                      track,
                                      0.0,
                                      track == laid->tracks[0] ? 36 : (track == laid->tracks[1] ? 34 : 72))
                        .has_value();
            check(everyRow, "une bande par piste : Kick, 808, Lead");
            snapshot("s18-toile-bloc-cadre");
        });

    add("S18 : un clic dans la bande Lead écrit une note dans le pattern ; l'autre bloc du pattern s'allume "
        "et "
        "la montre",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr || laid->placements.size() < 2)
                return;
            laid->depth = depth();
            laid->before = domain::json::write(state_.toValue());

            const auto at = view->notePointIn(laid->placements.front(), laid->tracks[2], 0.5, 73);
            check(at.has_value(), "la bande Lead a une rangée pour do#5");
            if (!at.has_value())
                return;

            hoverAt(*view, *at);
            check(view->litPattern() == laid->pattern,
                  "la main sur le bloc allume son pattern avant tout clic");
            click(*view, *at);
            check(depth() == laid->depth + 1, "une entrée d'historique");
        });

    add("S18 : la note est dans le pattern, et l'autre bloc du pattern la montre ; les deux blocs à l'écran, "
        "le second allumé",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr || laid->placements.size() < 2)
                return;

            const auto* pattern = state_.findPattern(laid->pattern);
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(laid->tracks[2]) : nullptr;
            if (row != nullptr)
            {
                for (const auto& note : row->notes)
                {
                    if (note.pitch == 73 && std::abs(note.startBeats - 0.5) < 1e-9)
                        laid->written = note.id;
                }
            }
            check(!laid->written.isNil(), "une note do#5 au temps 1,5 du pattern");
            if (laid->written.isNil())
                return;
            check(view->noteBoundsIn(laid->placements.back(), laid->written).has_value(),
                  "l'autre bloc du pattern montre la même note : modifié une fois, changé partout");

            // Both blocks in sight, still at the scale of notes, the hand on
            // the first: the second is lit.
            view->frameBlock(laid->placements.front());
            view->zoomAround(view->timelineArea().getPosition(), 0.4);
            check(view->notesGrabbable(), "les deux blocs à l'écran, les notes s'attrapent encore");
            if (const auto at = view->notePointIn(laid->placements.front(), laid->tracks[2], 0.5, 73);
                at.has_value())
                hoverAt(*view, *at);
            check(view->litPattern() == laid->pattern, "le pattern sous la main est allumé");
            snapshot("s18-toile-jumeaux-allumes");
            view->frameBlock(laid->placements.front());
        });

    add("S18 : glisser la note de deux demi-tons et d'une double-croche : une entrée ; Ctrl+Z à l'octet",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr || laid->written.isNil())
                return;
            const auto from = view->notePointIn(laid->placements.front(), laid->tracks[2], 0.5, 73);
            const auto to = view->notePointIn(laid->placements.front(), laid->tracks[2], 0.75, 75);
            check(from.has_value() && to.has_value(), "départ et arrivée dans la bande");
            if (!from.has_value() || !to.has_value())
                return;

            laid->depth = depth();
            drag(*view, *from, *to);
        });

    add("S18 : la note a bougé d'un seul geste ; deux Ctrl+Z rendent le projet à l'octet",
        [this, laid]
        {
            if (laid->written.isNil())
                return;
            const auto depthBefore = laid->depth;
            const auto* pattern = state_.findPattern(laid->pattern);
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(laid->tracks[2]) : nullptr;
            auto moved = false;
            if (row != nullptr)
            {
                for (const auto& note : row->notes)
                    moved = moved || (note.id == laid->written && note.pitch == 75 &&
                                      std::abs(note.startBeats - 0.75) < 1e-9);
            }
            check(moved, "la note est en ré#5, au temps 1,75");
            check(depth() == depthBefore + 1, "un seul geste, une seule entrée");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->before, "deux Ctrl+Z : le projet à l'octet");
        });
}

void Verification::addCanvasLoadSteps()
{
    const auto canvas = [this] { return dynamic_cast<ui::PlaylistPanel*>(panel("canvas")); };

    add(
        "S18 : repeint de la toile sur un projet chargé : 32 lignes, 256 blocs, 16 patterns de 4 pistes et "
        "128 "
        "notes ; le cache ne bouge pas avec la vue",
        [this, canvas]
        {
            if (canvas() == nullptr || !canvas()->isShowing())
                key(juce::KeyPress{juce::KeyPress::F4Key});
            auto* view = canvas();
            if (view == nullptr)
                return;

            std::vector<domain::TrackId> tracks;
            std::vector<std::unique_ptr<domain::Command>> commands;
            for (int track = 0; track < 4; ++track)
            {
                tracks.push_back(domain::TrackId::generate());
                commands.push_back(std::make_unique<domain::AddTrack>(
                    tracks.back(), "Charge " + std::to_string(track + 1), 0.0));
            }
            std::vector<domain::LaneId> lanes;
            for (int lane = 0; lane < 32; ++lane)
            {
                lanes.push_back(domain::LaneId::generate());
                commands.push_back(
                    std::make_unique<domain::CreateLane>(lanes.back(),
                                                         "Charge " + std::to_string(lane + 1),
                                                         state_.lanes().size() + lanes.size() - 1));
            }
            std::vector<domain::PatternId> patterns;
            for (int index = 0; index < 16; ++index)
            {
                patterns.push_back(domain::PatternId::generate());
                commands.push_back(std::make_unique<domain::CreatePattern>(
                    patterns.back(), "Charge " + std::to_string(index + 1), 16.0, false));
                for (std::size_t track = 0; track < tracks.size(); ++track)
                {
                    const auto row = domain::ClipId::generate();
                    commands.push_back(
                        std::make_unique<domain::AddPatternTrack>(patterns.back(), row, tracks[track]));
                    for (int step = 0; step < 32; ++step)
                    {
                        domain::Note note{};
                        note.id = domain::NoteId::generate();
                        note.pitch = 40 + static_cast<int>(track) * 12 + (step * 7 + index) % 12;
                        note.startBeats = step * 0.5;
                        note.lengthBeats = 0.25 + 0.25 * (step % 3);
                        commands.push_back(std::make_unique<domain::AddNote>(row, note));
                    }
                }
            }
            // 32 notes per row over 16 beats, 4 rows: 128 notes per pattern.
            for (int block = 0; block < 256; ++block)
                commands.push_back(
                    std::make_unique<domain::PlacePattern>(domain::PlacementId::generate(),
                                                           patterns[static_cast<std::size_t>(block % 16)],
                                                           static_cast<double>((block / 32) * 16),
                                                           lanes[static_cast<std::size_t>(block % 32)]));

            domain::GroupOptions group{};
            group.label = "toile : projet chargé";
            const auto started = juce::Time::getMillisecondCounterHiRes();
            check(bus_.executeGroup(std::move(commands), group).ok(), "le projet chargé est posé");
            note("projet chargé posé en " + ms(juce::Time::getMillisecondCounterHiRes() - started));
        },
        {},
        120000.0);

    add(
        "S18 : repeint de la toile chargée, à trois échelles et pendant un déplacement",
        [this, canvas]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;

            view->showWholeSong();
            const auto overview = medianPaintMs(*view, 20);
            const auto grid = view->timelineArea();
            const auto centre = grid.getCentre();

            // The scale where the grid shows, then the scale of notes.
            view->zoomAround(centre, 24.0 / std::max(1.0, view->beatWidth()));
            const auto approach = medianPaintMs(*view, 20);
            view->zoomAround(centre, 48.0 / std::max(1.0, view->beatWidth()));
            check(view->notesGrabbable(), "à 48 px par temps, les notes s'attrapent");
            const auto notes = medianPaintMs(*view, 20);

            // Twenty frames of a pan at the scale of notes: the cache is not
            // touched by a move of the view.
            const auto builds = view->bandBuilds();
            const auto panned =
                medianPaintMs(*view,
                              20,
                              [view, grid](int frame)
                              {
                                  juce::ignoreUnused(frame);
                                  view->zoomAround(grid.getCentre(), 1.0);
                                  view->mouseWheelMove(
                                      juce::MouseEvent{juce::Desktop::getInstance().getMainMouseSource(),
                                                       grid.getCentre().toFloat(),
                                                       juce::ModifierKeys{juce::ModifierKeys::shiftModifier},
                                                       juce::MouseInputSource::defaultPressure,
                                                       0.0f,
                                                       0.0f,
                                                       0.0f,
                                                       0.0f,
                                                       view,
                                                       view,
                                                       juce::Time::getCurrentTime(),
                                                       grid.getCentre().toFloat(),
                                                       juce::Time::getCurrentTime(),
                                                       0,
                                                       false},
                                      juce::MouseWheelDetails{0.0f, -0.05f, false, false, false});
                              });
            check(view->bandBuilds() == builds, "vingt images de déplacement : aucune rangée recalculée");

            note("repeint, médiane de 20 : vue d'ensemble " + ms(overview) + ", approche " + ms(approach) +
                 ", notes " + ms(notes) + ", déplacement " + ms(panned) + " (" +
                 std::to_string(view->getWidth()) + "×" + std::to_string(view->getHeight()) + ")");
            snapshot("s18-toile-projet-charge");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            view->showWholeSong();
        },
        {},
        60000.0);
}

} // namespace daw::app
