#include "Verification.h"
#include "VerificationTiming.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/panels/PianoRollPanel.h"
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

        // S19: a second pattern between the two blocks, on the same line.
        domain::PatternId other{};
        domain::PlacementId otherPlacement{};
        std::string beforePaste;
    };
    auto laid = std::make_shared<Laid>();

    // A row of a band may be under the bottom of a small page: the wheel
    // brings it up, as a hand would, before it is aimed at.
    const auto inSight = [this](ui::PlaylistPanel& view,
                                domain::PlacementId placement,
                                domain::TrackId track,
                                double beats,
                                int pitch)
    {
        auto at = view.notePointIn(placement, track, beats, pitch);
        for (int notch = 0; notch < 60 && at.has_value() && !view.timelineArea().contains(*at); ++notch)
        {
            wheel(view,
                  view.timelineArea().getCentre(),
                  at->getY() >= view.timelineArea().getBottom() ? -0.1f : 0.1f);
            at = view.notePointIn(placement, track, beats, pitch);
        }
        return at;
    };

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
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr || laid->placements.size() < 2)
                return;
            laid->depth = depth();
            laid->before = domain::json::write(state_.toValue());

            const auto at = inSight(*view, laid->placements.front(), laid->tracks[2], 0.5, 73);
            check(at.has_value(), "la bande Lead a une rangée pour do#5");
            check(at.has_value() && view->timelineArea().contains(*at),
                  "la rangée est à l'écran avant d'être visée");
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
        [this, canvas, laid, inSight]
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
            if (const auto at = inSight(*view, laid->placements.front(), laid->tracks[2], 0.5, 73);
                at.has_value())
                hoverAt(*view, *at);
            check(view->litPattern() == laid->pattern, "le pattern sous la main est allumé");
            snapshot("s18-toile-jumeaux-allumes");
            view->frameBlock(laid->placements.front());
        });

    add("S18 : glisser la note de deux demi-tons et d'une double-croche : une entrée ; Ctrl+Z à l'octet",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr || laid->written.isNil())
                return;
            const auto from = inSight(*view, laid->placements.front(), laid->tracks[2], 0.5, 73);
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

    // S19: the selection of notes on the canvas.
    add("S19 : un second pattern, Lead seul, posé entre les deux blocs sur la même ligne",
        [this, laid]
        {
            if (laid->placements.size() < 2)
                return;
            const auto* first = state_.findPlacement(laid->placements.front());
            if (first == nullptr)
                return;

            std::vector<std::unique_ptr<domain::Command>> commands;
            laid->other = domain::PatternId::generate();
            commands.push_back(std::make_unique<domain::CreatePattern>(laid->other, "Toile B", 4.0));
            const auto row = domain::ClipId::generate();
            commands.push_back(std::make_unique<domain::AddPatternTrack>(laid->other, row, laid->tracks[2]));
            for (int step = 0; step < 4; ++step)
            {
                domain::Note made{};
                made.id = domain::NoteId::generate();
                made.pitch = step % 2 == 0 ? 74 : 76;
                made.startBeats = step * 1.0;
                made.lengthBeats = 0.5;
                commands.push_back(std::make_unique<domain::AddNote>(row, made));
            }
            laid->otherPlacement = domain::PlacementId::generate();
            commands.push_back(std::make_unique<domain::PlacePattern>(
                laid->otherPlacement, laid->other, first->startBeats + 4.0, first->laneId));
            domain::GroupOptions group{};
            group.label = "toile : un second pattern";
            check(bus_.executeGroup(std::move(commands), group).ok(),
                  "Toile B est posé entre les deux blocs");
        });

    add("S19 : Ctrl + glisser à l'échelle des notes, d'un bloc à l'autre à travers Toile B",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr || laid->otherPlacement.isNil())
                return;

            // The three blocks in sight, at the scale of notes.
            view->frameBlock(laid->placements.front());
            view->zoomAround(view->timelineArea().getPosition(), 36.0 / std::max(1.0, view->beatWidth()));
            check(view->notesGrabbable(), "36 px par temps : les notes s'attrapent");

            // From an empty row of the Lead band, early in the first block,
            // to an empty row early in the last one: the ré of beat 1 is in
            // both, one note seen twice.
            const auto from = inSight(*view, laid->placements.front(), laid->tracks[2], 0.9, 79);
            const auto to = view->notePointIn(laid->placements.back(), laid->tracks[2], 1.25, 71);
            check(from.has_value() && to.has_value() && view->timelineArea().contains(*from) &&
                      view->timelineArea().contains(*to),
                  "les deux coins de la bande sont à l'écran, dans la bande Lead");
            if (!from.has_value() || !to.has_value())
                return;
            drag(*view, *from, *to, true);
        });

    add("S19 : la bande a pris huit notes : les quatre du premier pattern, une fois chacune, et les quatre "
        "de "
        "Toile B ; aucun bloc",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            const auto& picked = view->pickedNotes();

            const auto* pattern = state_.findPattern(laid->pattern);
            const auto* lead = pattern != nullptr ? pattern->findClipForTrack(laid->tracks[2]) : nullptr;
            const auto* other = state_.findPattern(laid->other);
            const auto* otherLead = other != nullptr ? other->findClipForTrack(laid->tracks[2]) : nullptr;
            if (lead == nullptr || otherLead == nullptr)
            {
                check(false, "les deux rangées Lead existent");
                return;
            }

            const auto pickedOne = [&picked](domain::NoteId id)
            {
                return std::count_if(picked.begin(),
                                     picked.end(),
                                     [id](const ui::PlaylistPanel::PickedNote& one)
                                     { return one.note == id; });
            };
            std::vector<int> pitches;
            for (const auto& note : lead->notes)
                if (pickedOne(note.id) == 1)
                    pitches.push_back(note.pitch);
            std::sort(pitches.begin(), pitches.end());
            check(pitches == std::vector<int>{72, 74, 76, 78},
                  "du premier pattern : do, ré, mi, fa#, une fois chacune ; le ré, dans les deux blocs, "
                  "une seule fois");
            auto allOther = true;
            for (const auto& note : otherLead->notes)
                allOther = allOther && pickedOne(note.id) == 1;
            check(allOther, "les quatre notes de Toile B");
            check(picked.size() == 8, "huit notes en tout : " + std::to_string(picked.size()));
            check(view->selected().empty(), "aucun bloc choisi : notes et blocs ne vivent pas ensemble");
            snapshot("s19-toile-bande-de-notes");
        });

    add("S19 : au-dessus du seuil, la même bande prend des blocs, et lâche les notes",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr || laid->placements.size() < 2)
                return;
            const auto* first = state_.findPlacement(laid->placements.front());
            if (first == nullptr)
                return;
            const auto lane = state_.laneIndex(first->laneId);
            if (!lane)
                return;

            view->showWholeSong();
            check(!view->notesGrabbable(), "dézoomée, les notes ne s'attrapent pas");
            const auto from = view->pointFor(static_cast<int>(lane.value()), first->startBeats - 1.0);
            const auto to = view->pointFor(static_cast<int>(lane.value()), first->startBeats + 11.5);
            drag(*view, from, to.translated(0, 2), true);
        });

    add("S19 : trois blocs choisis, aucune note",
        [this, canvas]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            check(view->selected().size() == 3,
                  "les trois blocs de la ligne : " + std::to_string(view->selected().size()));
            check(view->pickedNotes().empty(), "les notes sont lâchées");
            snapshot("s19-toile-bande-de-blocs");
            static_cast<void>(view->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
        });

    // S19: copy, paste, duplicate, on the canvas.
    const auto leadOf = [this](domain::PatternId patternId, domain::TrackId track) -> const domain::Clip*
    {
        const auto* pattern = state_.findPattern(patternId);
        return pattern != nullptr ? pattern->findClipForTrack(track) : nullptr;
    };

    add("S19 : la bande prend les quatre notes Lead du premier bloc ; Ctrl+C",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr || laid->otherPlacement.isNil())
                return;
            view->frameBlock(laid->placements.front());
            view->zoomAround(view->timelineArea().getPosition(), 36.0 / std::max(1.0, view->beatWidth()));
            const auto from = inSight(*view, laid->placements.front(), laid->tracks[2], 0.1, 79);
            const auto to = view->notePointIn(laid->placements.front(), laid->tracks[2], 3.7, 71);
            if (!from.has_value() || !to.has_value())
            {
                check(false, "la bande Lead du premier bloc est à l'écran");
                return;
            }
            drag(*view, *from, *to, true);
        });

    add("S19 : Ctrl+V la main sur Toile B, au temps 3 : deux notes collées, deux hors du pattern, une entrée",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            check(view->pickedNotes().size() == 4,
                  "quatre notes choisies dans le premier bloc : " +
                      std::to_string(view->pickedNotes().size()));
            static_cast<void>(view->keyPressed(juce::KeyPress{'c', juce::ModifierKeys::ctrlModifier, 0}));

            const auto aim = view->notePointIn(laid->otherPlacement, laid->tracks[2], 2.1, 79);
            if (!aim.has_value() || !view->timelineArea().contains(*aim))
            {
                check(false, "Toile B est à l'écran sous la main");
                return;
            }
            laid->beforePaste = domain::json::write(state_.toValue());
            laid->depth = depth();
            hoverAt(*view, *aim);
            static_cast<void>(view->keyPressed(juce::KeyPress{'v', juce::ModifierKeys::ctrlModifier, 0}));
        });

    add("S19 : les notes collées sont dans Toile B, au bon temps, et dessinées choisies",
        [this, canvas, laid, leadOf]
        {
            auto* view = canvas();
            const auto* lead = leadOf(laid->other, laid->tracks[2]);
            if (view == nullptr || lead == nullptr)
                return;
            check(depth() == laid->depth + 1, "une entrée d'historique");
            check(lead->notes.size() == 6,
                  "Toile B a six notes Lead : " + std::to_string(lead->notes.size()));
            const auto has = [lead](int pitch, double beats)
            {
                return std::any_of(lead->notes.begin(),
                                   lead->notes.end(),
                                   [&](const domain::Note& note) {
                                       return note.pitch == pitch && std::abs(note.startBeats - beats) < 1e-9;
                                   });
            };
            check(has(72, 2.0) && has(74, 3.0), "do au temps 3, ré au temps 4 : l'écart d'origine gardé");
            const auto label = history_.cursor() > 0
                                   ? std::string{history_.entries()[history_.cursor() - 1].label()}
                                   : std::string{};
            check(label.find("2 hors du pattern") != std::string::npos,
                  "l'historique dit les deux notes laissées hors du pattern : « " + label + " »");

            // At the render: the pasted do, painted where the canvas says it
            // is, in the colour of picked notes.
            const auto& picked = view->pickedNotes();
            check(picked.size() == 2, "les deux notes collées sont les notes choisies");
            for (const auto& one : picked)
            {
                const auto bounds = view->noteBoundsIn(laid->otherPlacement, one.note);
                if (!bounds.has_value())
                {
                    check(false, "la note collée est à l'écran");
                    continue;
                }
                const auto image = view->createComponentSnapshot(*bounds, false, 1.0f);
                const auto centre = image.getPixelAt(image.getWidth() / 2, image.getHeight() / 2);
                const auto wanted = tokens_.colour("color.note.selected");
                check(std::abs(centre.getRed() - wanted.getRed()) < 8 &&
                          std::abs(centre.getGreen() - wanted.getGreen()) < 8 &&
                          std::abs(centre.getBlue() - wanted.getBlue()) < 8,
                      "au rendu, la note collée est peinte choisie : " +
                          centre.toDisplayString(false).toStdString());
            }
            snapshot("s19-toile-collage");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->beforePaste, "Ctrl+Z : le projet à l'octet");
        });

    add("S19 : Ctrl+B sur les quatre notes Lead d'un bloc : le pattern s'allonge d'une mesure, une entrée",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            const auto from = inSight(*view, laid->placements.front(), laid->tracks[2], 0.1, 79);
            const auto to = view->notePointIn(laid->placements.front(), laid->tracks[2], 3.7, 71);
            if (!from.has_value() || !to.has_value())
                return;
            laid->beforePaste = domain::json::write(state_.toValue());
            laid->depth = depth();
            drag(*view, *from, *to, true);
        });

    add("S19 : le premier pattern dure deux mesures, ses huit notes Lead ; Ctrl+Z à l'octet",
        [this, canvas, laid, leadOf]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            check(view->pickedNotes().size() == 4,
                  "quatre notes choisies : " + std::to_string(view->pickedNotes().size()));
            static_cast<void>(view->keyPressed(juce::KeyPress{'b', juce::ModifierKeys::ctrlModifier, 0}));
            const auto* pattern = state_.findPattern(laid->pattern);
            const auto* lead = leadOf(laid->pattern, laid->tracks[2]);
            check(pattern != nullptr && pattern->lengthBeats == 8.0, "le pattern dure deux mesures");
            check(lead != nullptr && lead->notes.size() == 8, "huit notes Lead");
            check(depth() == laid->depth + 1, "une entrée d'historique");
            snapshot("s19-toile-duplique");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->beforePaste, "Ctrl+Z : le projet à l'octet");
            static_cast<void>(view->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
        });

    // S18, the grammar: the same keys and wheel in the piano roll.
    auto widthBefore = std::make_shared<double>(0.0);
    add("S18 : F dans le piano-roll cadre les notes du pattern ; Maj+F montre le pattern entier",
        [this, laid]
        {
            selection_.selectPattern(laid->pattern);
            selection_.selectTrack(laid->tracks[2]);
            if (panel("piano_roll") == nullptr || !panel("piano_roll")->isShowing())
                key(juce::KeyPress{juce::KeyPress::F7Key});
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            check(roll != nullptr && roll->isShowing(), "F7 ouvre le piano-roll sur le Lead");
            if (roll == nullptr)
                return;

            static_cast<void>(roll->keyPressed(juce::KeyPress{'f', 0, 'f'}));
            const auto* pattern = state_.findPattern(laid->pattern);
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(laid->tracks[2]) : nullptr;
            auto inSight = row != nullptr && !row->notes.empty();
            if (row != nullptr)
            {
                for (const auto& note : row->notes)
                {
                    const auto bounds = roll->noteBounds(note);
                    inSight = inSight && bounds.getY() >= roll->ruler().getBottom() &&
                              bounds.getBottom() <= roll->velocityLane().getY() &&
                              bounds.getX() >= roll->ruler().getX() &&
                              bounds.getRight() <= roll->ruler().getRight();
                }
            }
            check(inSight, "F : toutes les notes du Lead à l'écran, dans la grille");
            snapshot("s18-piano-roll-cadre");

            static_cast<void>(roll->keyPressed(juce::KeyPress{'F', juce::ModifierKeys::shiftModifier, 'F'}));
            check(roll->firstBeat() == 0.0, "Maj+F : le pattern depuis son début");
        });

    add(
        "S18 : Ctrl + molette zoome dans le piano-roll, comme dans la toile",
        [this, widthBefore]
        {
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            if (roll == nullptr)
                return;
            *widthBefore = roll->beatWidth();
            const auto ruler = roll->ruler();
            wheel(*roll,
                  {ruler.getCentreX(), roll->velocityLane().getY() - ruler.getHeight()},
                  0.5f,
                  false,
                  true);
        },
        [this, widthBefore]
        {
            // On the fluid pace the zoom glides: polled until it has grown.
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            return roll != nullptr && roll->beatWidth() > *widthBefore + 1.0;
        });

    add("S18 : le zoom a grandi ; F7 referme le piano-roll",
        [this, widthBefore]
        {
            auto* roll = dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll"));
            check(roll != nullptr && roll->beatWidth() > *widthBefore + 1.0,
                  "Ctrl + molette dans la grille : le temps zoome, les hauteurs ne défilent pas");
            key(juce::KeyPress{juce::KeyPress::F7Key});
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

            // Each scale timed twice: in Direct2D, the window's renderer, and
            // in software, the light mode's and the S18 figure's. S19: the
            // target is 8 ms at the 95th percentile in Direct2D, everywhere.
            struct Row
            {
                std::string name;
                timing::Timing direct2d;
                timing::Timing software;
            };
            std::vector<Row> rows;
            const auto both = [&rows, view](std::string name, const std::function<void(int)>& between = {})
            {
                rows.push_back({std::move(name),
                                timing::measure(*view, juce::NativeImageType{}, 5, 60, between),
                                timing::measure(*view, juce::SoftwareImageType{}, 5, 60, between)});
            };

            view->showWholeSong();
            both("vue d'ensemble");
            const auto grid = view->timelineArea();
            const auto centre = grid.getCentre();

            // The scale where the grid shows, then the scale of notes.
            view->zoomAround(centre, 24.0 / std::max(1.0, view->beatWidth()));
            both("approche (24 px par temps)");
            view->zoomAround(centre, 48.0 / std::max(1.0, view->beatWidth()));
            check(view->notesGrabbable(), "à 48 px par temps, les notes s'attrapent");
            both("notes (48 px par temps)");

            // Frames of a pan at the scale of notes, there and back: the
            // cache is not touched by a move of the view.
            const auto builds = view->bandBuilds();
            const auto images = view->imageBuilds();
            both("une image de déplacement",
                 [view, grid](int frame)
                 {
                     const auto delta = (frame / 10) % 2 == 0 ? -0.05f : 0.05f;
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
                         juce::MouseWheelDetails{0.0f, delta, false, false, false});
                 });
            check(view->bandBuilds() == builds, "le déplacement : aucune rangée recalculée");
            check(view->imageBuilds() == images, "le déplacement : aucune image de bloc redessinée");

            // The playlist of S11 on the same song, for the scale of blocks.
            if (auto* playlist = panel("playlist"); playlist != nullptr && playlist->getWidth() > 0)
                note("la playlist, vue d'ensemble, Direct2D : " +
                     timing::describe(timing::measure(*playlist, juce::NativeImageType{}, 5, 60)));

            note("page de " + std::to_string(view->getWidth()) + "×" + std::to_string(view->getHeight()) +
                 " px, échelle " +
                 juce::String(juce::Component::getApproximateScaleFactorForComponent(view), 2).toStdString());
            for (const auto& row : rows)
            {
                note(row.name + ", Direct2D : " + timing::describe(row.direct2d));
                note(row.name + ", logiciel : " + timing::describe(row.software));
                check(row.direct2d.p95 < 8.0,
                      row.name + " sous 8 ms au 95e centile en Direct2D : " +
                          juce::String(row.direct2d.p95, 2).toStdString() + " ms");
            }
            snapshot("s18-toile-projet-charge");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            view->showWholeSong();
        },
        {},
        60000.0);
}

} // namespace daw::app
