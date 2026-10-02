#include "Verification.h"
#include "VerificationTiming.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/panels/PianoRollPanel.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <map>
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
        domain::NoteId offGrid{};
        double songFirstBeat{0.0};
        domain::PatternId fresh{};
        domain::TrackId pad{};
        std::size_t padDepth{0};
        std::vector<domain::generation::GhostNote> firstGhosts;
        std::map<std::string, int> pitches; // of the picked notes, by identifier
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
        for (int notch = 0; notch < 400 && at.has_value() && !view.timelineArea().contains(*at); ++notch)
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

            // On a long project the line may be under the bottom: the wheel
            // brings it up, as a hand would.
            const auto laneIndex = static_cast<int>(lane.value());
            for (int notch = 0; notch < 400; ++notch)
            {
                const auto at = view->pointFor(laneIndex, first->startBeats);
                if (view->timelineArea().contains(at.translated(0, 4)))
                    break;
                wheel(*view,
                      view->timelineArea().getCentre(),
                      at.getY() >= view->timelineArea().getBottom() - 4 ? -0.1f : 0.1f);
            }
            const auto from = view->pointFor(laneIndex, first->startBeats - 1.0);
            const auto to = view->pointFor(laneIndex, first->startBeats + 11.5);
            check(view->timelineArea().contains(from.translated(0, 2)),
                  "la ligne est à l'écran avant la bande");
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

    // S19: quantise and transpose the picked notes, across two patterns.
    const auto pickedPitches = [this](const ui::PlaylistPanel& view)
    {
        std::map<std::string, int> found;
        for (const auto& one : view.pickedNotes())
            for (const auto& pattern : state_.patterns())
                for (const auto& row : pattern.clips)
                    for (const auto& n : row.notes)
                        if (n.id == one.note)
                            found[n.id.toString()] = n.pitch;
        return found;
    };

    add("S19 : une note hors de la grille dans Toile B",
        [this, canvas, laid, leadOf]
        {
            auto* view = canvas();
            const auto* lead = leadOf(laid->other, laid->tracks[2]);
            if (view == nullptr || lead == nullptr)
                return;
            domain::Note off{};
            off.id = domain::NoteId::generate();
            off.pitch = 81;
            off.startBeats = 0.6;
            off.lengthBeats = 0.25;
            laid->offGrid = off.id;
            check(bus_.execute(std::make_unique<domain::AddNote>(lead->id, off)).ok(),
                  "un la5 au temps 1,6 de Toile B, hors de la double-croche");
        });

    // A step of its own: the canvas hears the note before the band is aimed.
    add("S19 : la bande prend les Lead du premier bloc et de Toile B",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            view->frameBlock(laid->placements.front());
            view->zoomAround(view->timelineArea().getPosition(), 36.0 / std::max(1.0, view->beatWidth()));
            const auto from = inSight(*view, laid->placements.front(), laid->tracks[2], 0.1, 83);
            const auto to = view->notePointIn(laid->otherPlacement, laid->tracks[2], 3.7, 71);
            if (!from.has_value() || !to.has_value())
            {
                check(false, "la bande va du premier bloc à Toile B");
                return;
            }
            drag(*view, *from, *to, true);
        });

    add("S19 : flèche haut : les neuf notes montent d'un demi-ton, une entrée",
        [this, canvas, laid, pickedPitches]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            laid->pitches = pickedPitches(*view);
            check(laid->pitches.size() == 9, "neuf notes choisies : " + std::to_string(laid->pitches.size()));
            laid->beforePaste = domain::json::write(state_.toValue());
            laid->depth = depth();

            static_cast<void>(view->keyPressed(juce::KeyPress{juce::KeyPress::upKey}));
            const auto now = pickedPitches(*view);
            auto allUp = now.size() == laid->pitches.size();
            for (const auto& [id, pitch] : laid->pitches)
                allUp = allUp && now.count(id) == 1 && now.at(id) == pitch + 1;
            check(allUp, "chaque note un demi-ton plus haut, dans les deux patterns");
            check(depth() == laid->depth + 1, "une entrée d'historique");
        });

    add("S19 : Ctrl+Q : la note hors de la grille revient à la double-croche, les autres ne bougent pas",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            static_cast<void>(view->keyPressed(juce::KeyPress{'q', juce::ModifierKeys::ctrlModifier, 0}));
            double start = -1.0;
            for (const auto& pattern : state_.patterns())
                for (const auto& row : pattern.clips)
                    for (const auto& n : row.notes)
                        if (n.id == laid->offGrid)
                            start = n.startBeats;
            check(std::abs(start - 0.5) < 1e-9, "le la#5 est au temps 1,5 : " + std::to_string(start));
            check(depth() == laid->depth + 2, "une entrée d'historique");
            snapshot("s19-toile-transpose-quantifie");
        });

    add("S19 : Ctrl+flèche haut, trois octaves passent ; la quatrième sortirait du clavier : refusée en "
        "entier",
        [this, canvas, laid, pickedPitches]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            const auto ctrlUp = juce::KeyPress{juce::KeyPress::upKey, juce::ModifierKeys::ctrlModifier, 0};
            for (int octave = 0; octave < 3; ++octave)
                static_cast<void>(view->keyPressed(ctrlUp));
            check(depth() == laid->depth + 5, "trois octaves, trois entrées");
            const auto before = pickedPitches(*view);
            static_cast<void>(view->keyPressed(ctrlUp));
            check(depth() == laid->depth + 5, "la quatrième : aucune entrée");
            check(pickedPitches(*view) == before,
                  "et aucune note n'a bougé, pas même celles qui auraient pu");

            for (int entry = 0; entry < 5; ++entry)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->beforePaste,
                  "cinq Ctrl+Z : le projet à l'octet");
            static_cast<void>(view->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
        });

    // S19: the velocity strip, under the band of the chosen track.
    add("S19 : le Lead choisi dans le rack : la bande de vélocité s'ouvre sous lui",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            selection_.selectTrack(laid->tracks[2]);
        });

    add("S19 : le trait sur la bande de vélocité, de gauche à droite, de bas en haut",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            view->frameBlock(laid->placements.front());

            // The Lead is the last band: its strip may be under the bottom.
            auto strip = view->velocityStripIn(laid->placements.front());
            for (int notch = 0; notch < 60 && strip.has_value() &&
                                !view->timelineArea().contains(strip->withHeight(strip->getHeight() - 1));
                 ++notch)
            {
                wheel(*view, view->timelineArea().getCentre(), -0.1f);
                strip = view->velocityStripIn(laid->placements.front());
            }
            check(strip.has_value() && !strip->isEmpty(), "une bande de vélocité sous le Lead, dans le bloc");
            if (!strip.has_value() || strip->isEmpty())
                return;
            check(strip->getHeight() == tokens_.integer("metric.canvas.velocityStrip"),
                  "haute de " + std::to_string(strip->getHeight()) + " px");
            laid->beforePaste = domain::json::write(state_.toValue());
            laid->depth = depth();
            drag(*view,
                 {strip->getX() + 2, strip->getBottom() - 2},
                 {strip->getRight() - 2, strip->getY() + 2});
        });

    add("S19 : un crescendo sur les quatre notes du Lead, une entrée, et les tiges dessinées à leur hauteur",
        [this, canvas, laid, leadOf]
        {
            auto* view = canvas();
            const auto* lead = leadOf(laid->pattern, laid->tracks[2]);
            if (view == nullptr || lead == nullptr)
                return;
            auto notes = lead->notes;
            std::sort(notes.begin(),
                      notes.end(),
                      [](const domain::Note& a, const domain::Note& b)
                      { return a.startBeats < b.startBeats; });
            std::string list;
            auto rising = notes.size() == 4;
            for (std::size_t index = 0; index < notes.size(); ++index)
            {
                list += std::to_string(notes[index].velocity) + " ";
                if (index > 0)
                    rising = rising && notes[index].velocity > notes[index - 1].velocity;
            }
            // The last stem is three quarters along the stroke, which ends
            // at the top: loud, not the loudest.
            check(rising && notes.front().velocity < 50 && notes.back().velocity > 90,
                  "les vélocités montent, de doux à fort : " + list);
            check(depth() == laid->depth + 1, "une entrée d'historique");

            // At the render: the stem of the loudest note reaches near the
            // top of the strip, the softest stays near the bottom.
            const auto strip = view->velocityStripIn(laid->placements.front());
            const auto loud = view->noteBoundsIn(laid->placements.front(), notes.back().id);
            const auto soft = view->noteBoundsIn(laid->placements.front(), notes.front().id);
            if (strip.has_value() && loud.has_value() && soft.has_value())
            {
                const auto image = view->createComponentSnapshot(*strip, false, 1.0f);
                const auto sunken = tokens_.colour("color.surface.sunken");
                const auto stemAt = [&](int x, int y)
                {
                    const auto pixel = image.getPixelAt(x - strip->getX() + 1, y - strip->getY());
                    return std::abs(pixel.getRed() - sunken.getRed()) +
                               std::abs(pixel.getGreen() - sunken.getGreen()) +
                               std::abs(pixel.getBlue() - sunken.getBlue()) >
                           60;
                };
                const auto high = strip->getY() + strip->getHeight() / 4;
                check(stemAt(loud->getX(), high), "au rendu, la tige forte monte dans le haut de la bande");
                check(!stemAt(soft->getX(), high), "au rendu, la tige douce reste en bas");
            }
            snapshot("s19-toile-velocites");
        });

    add(
        "S19 : Alt + molette sur une note : sa vélocité, deux crans, une entrée ; Ctrl+Z à l'octet",
        [this, canvas, laid, leadOf]
        {
            auto* view = canvas();
            const auto* lead = leadOf(laid->pattern, laid->tracks[2]);
            if (view == nullptr || lead == nullptr || lead->notes.empty())
                return;
            const auto target = lead->notes.front();
            const auto bounds = view->noteBoundsIn(laid->placements.front(), target.id);
            check(bounds.has_value() && view->timelineArea().contains(bounds->getCentre()),
                  "la note est à l'écran");
            if (!bounds.has_value())
                return;
            wheel(*view, bounds->getCentre(), 1.0f, false, false, true);
            wheel(*view, bounds->getCentre(), 1.0f, false, false, true);
            const auto* after = leadOf(laid->pattern, laid->tracks[2]);
            auto velocity = -1;
            for (const auto& note : after->notes)
                if (note.id == target.id)
                    velocity = note.velocity;
            const auto step = tokens_.integer("metric.canvas.velocityWheelStep");
            check(velocity == std::min(127, target.velocity + 2 * step),
                  "deux crans : " + std::to_string(target.velocity) + " vers " + std::to_string(velocity));
            check(depth() == laid->depth + 2, "les deux crans font une seule entrée");
        },
        // The wheel at rest closes its gesture.
        [this] { return !bus_.openGesture().has_value(); },
        3000.0);

    add("S19 : deux Ctrl+Z : le projet à l'octet",
        [this, laid]
        {
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->beforePaste,
                  "deux Ctrl+Z : le projet à l'octet");
        });

    // S19: the pattern mode.
    add("S19 : PAT : la toile montre le pattern en cours seul, à l'origine, à l'échelle des notes",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            selection_.selectPattern(laid->pattern);
            // A view of the song far from the origin, to be found again.
            view->frameBlock(laid->placements.back());
            laid->songFirstBeat = view->firstBeat();
            check(laid->songFirstBeat > 4.0, "la vue du morceau est loin de l'origine");
            press("PAT");
        });

    add("S19 : une bande par canal du rack, le pattern cadré ; Toile B n'est pas là",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            check(view->showsPattern(), "la toile est en mode pattern");
            check(view->notesGrabbable(), "cadré à l'échelle des notes");
            check(view->firstBeat() <= 0.0 + 1e-9, "le pattern commence à gauche, à l'origine");
            auto everyBand = true;
            for (std::size_t index = 0; index < laid->tracks.size(); ++index)
                everyBand = everyBand && view->notePointIn(view->patternBlock(),
                                                           laid->tracks[index],
                                                           0.0,
                                                           index == 0 ? 36 : (index == 1 ? 34 : 72))
                                             .has_value();
            check(everyBand, "Kick, 808, Lead : trois bandes");
            check(!view->notePointIn(laid->otherPlacement, laid->tracks[2], 0.0, 74).has_value(),
                  "Toile B, posé sur la même ligne, n'est pas montré");

            // At the render: a kick, and the empty sixteenth after it, in the
            // kick's band, are not the same colour.
            const auto onNote = inSight(*view, view->patternBlock(), laid->tracks[0], 0.0, 36);
            const auto offNote = view->notePointIn(view->patternBlock(), laid->tracks[0], 0.5, 36);
            if (onNote.has_value() && offNote.has_value())
            {
                const auto image = view->createComponentSnapshot(view->getLocalBounds(), false, 1.0f);
                const auto a = image.getPixelAt(onNote->getX(), onNote->getY());
                const auto b = image.getPixelAt(offNote->getX(), offNote->getY());
                check(std::abs(a.getRed() - b.getRed()) + std::abs(a.getGreen() - b.getGreen()) +
                              std::abs(a.getBlue() - b.getBlue()) >
                          60,
                      "au rendu, le kick se détache de la case vide : " +
                          a.toDisplayString(false).toStdString() + " contre " +
                          b.toDisplayString(false).toStdString());
            }
            else
                check(false, "le kick est à l'écran");
            snapshot("s19-toile-mode-pattern");
        });

    add("S19 : « + Pattern » : un pattern neuf, vide, s'ouvre dans la toile avec ses trois bandes",
        [this] { press("+ Pattern"); });

    add("S19 : un clic dans la bande Lead du pattern neuf : la rangée s'ouvre et la note s'écrit, une entrée",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            const auto* fresh = state_.findPattern(selection_.pattern());
            check(fresh != nullptr && fresh->id != laid->pattern && fresh->clips.empty(),
                  "le pattern choisi est neuf et vide");
            if (fresh == nullptr)
                return;
            laid->fresh = fresh->id;
            check(view->showsPattern(), "la toile le montre");
            const auto at = inSight(*view, view->patternBlock(), laid->tracks[2], 0.5, 64);
            check(at.has_value() && view->timelineArea().contains(*at), "la bande Lead est à l'écran, vide");
            if (!at.has_value())
                return;
            laid->before = domain::json::write(state_.toValue());
            laid->depth = depth();
            click(*view, *at);
        });

    add("S19 : le pattern neuf a une rangée Lead et sa note ; Ctrl+Z le rend vide",
        [this, laid]
        {
            const auto* fresh = state_.findPattern(laid->fresh);
            const auto* row = fresh != nullptr ? fresh->findClipForTrack(laid->tracks[2]) : nullptr;
            check(row != nullptr && row->notes.size() == 1 && row->notes.front().pitch == 64 &&
                      std::abs(row->notes.front().startBeats - 0.5) < 1e-9,
                  "une rangée Lead, un mi4 au temps 1,5");
            check(depth() == laid->depth + 1, "une entrée d'historique");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->before,
                  "Ctrl+Z : le pattern vide, à l'octet");
        });

    add("S19 : SONG : la toile retrouve le morceau, et sa vue",
        [this, laid]
        {
            selection_.selectPattern(laid->pattern);
            press("SONG");
        });

    add("S19 : le morceau est revenu, à la même place",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            check(!view->showsPattern(), "la toile montre le morceau");
            check(std::abs(view->firstBeat() - laid->songFirstBeat) < 1e-6, "la vue d'avant PAT");
            check(view->notePointIn(laid->otherPlacement, laid->tracks[2], 0.0, 74).has_value(),
                  "Toile B est de nouveau sur sa ligne");
        });

    // S19: the generation in a band, in the empty Lead of the new pattern, on
    // PAT: nothing to rework, so a proposal of its own, with variants; and
    // Tab opens the row as it writes.
    add("S19 : PAT sur le pattern neuf",
        [this, laid]
        {
            selection_.selectPattern(laid->fresh);
            press("PAT");
        });

    add("S19 : Maj + glisser dans la bande Lead du pattern neuf : une zone de deux temps",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            check(view->showsPattern(), "la toile montre le pattern neuf");
            const auto from = inSight(*view, view->patternBlock(), laid->tracks[2], 0.0, 64);
            const auto to = view->notePointIn(view->patternBlock(), laid->tracks[2], 1.8, 64);
            if (!from.has_value() || !to.has_value())
            {
                check(false, "la bande Lead est à l'écran");
                return;
            }
            drag(*view, *from, *to, false, false, true);
        });

    add("S19 : Ctrl+G, « une mélodie en la mineur », Entrée : des notes grises dans la zone, au rendu",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            const auto zone = view->bandZone();
            check(zone.has_value() && zone->first == 0.0 && zone->second == 2.0,
                  "la zone va du temps 1 au temps 3 du pattern");
            laid->before = domain::json::write(state_.toValue());
            laid->depth = depth();

            static_cast<void>(view->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            check(view->generationBar().isShowing(), "la fenêtre s'ouvre sous la toile");
            check(view->generationBar().getY() >= view->timelineArea().getBottom(),
                  "sous la grille, jamais sur les notes");
            view->generationBar().field().setText(juce::String::fromUTF8("une mélodie en la mineur"), false);
            static_cast<void>(
                view->generationBar().field().keyPressed(juce::KeyPress{juce::KeyPress::returnKey}));

            const auto ghosts = view->ghostNotes();
            check(!ghosts.empty(), "des notes proposées : " + std::to_string(ghosts.size()));
            const domain::generation::Key aMinor{9, domain::generation::Mode::minor};
            auto inZone = true;
            auto inKey = true;
            for (const auto& ghost : ghosts)
            {
                inZone = inZone && ghost.startBeats >= -1e-9 && ghost.startBeats < 2.0;
                inKey = inKey && domain::generation::inScale(ghost.pitch, aMinor);
            }
            check(inZone, "toutes dans la zone");
            check(inKey, "toutes en la mineur");
            check(domain::json::write(state_.toValue()) == laid->before, "rien n'est écrit avant Tab");
            laid->firstGhosts = ghosts;

            // At the render: where the first grey note is drawn, the screen
            // changed from before the prompt.
            if (!ghosts.empty())
            {
                const auto at = inSight(*view,
                                        view->patternBlock(),
                                        laid->tracks[2],
                                        ghosts.front().startBeats,
                                        ghosts.front().pitch);
                if (at.has_value() && view->timelineArea().contains(*at))
                {
                    // A light grey: not the dark grid, not the lime of a
                    // written note.
                    const auto now = view->createComponentSnapshot(view->getLocalBounds(), false, 1.0f);
                    const auto colour = now.getPixelAt(at->getX(), at->getY());
                    const auto r = colour.getRed();
                    const auto gr = colour.getGreen();
                    const auto b = colour.getBlue();
                    check(r > 120 && gr > 120 && b > 120 && std::abs(r - gr) < 30 && std::abs(gr - b) < 30,
                          "au rendu, la note grise est dessinée en gris : " +
                              colour.toDisplayString(false).toStdString());
                }
                else
                    check(false, "la première note grise est à l'écran");
            }
            snapshot("s19-toile-generation");
        });

    add("S19 : Alt + molette sur la zone : une autre variante, puis la première",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr || laid->firstGhosts.empty())
                return;
            const auto at = inSight(*view, view->patternBlock(), laid->tracks[2], 0.5, 64);
            if (!at.has_value())
                return;
            wheel(*view, *at, -1.0f, false, false, true);
            check(view->ghostNotes() != laid->firstGhosts, "d'autres notes grises");
            wheel(*view, *at, 1.0f, false, false, true);
            check(view->ghostNotes() == laid->firstGhosts, "en arrière : la première variante");
        });

    add("S19 : Tab : la rangée Lead s'ouvre et reçoit les notes, une entrée ; Ctrl+Z à l'octet",
        [this, canvas, laid, leadOf]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            const auto ghosts = view->ghostNotes();
            static_cast<void>(view->keyPressed(juce::KeyPress{juce::KeyPress::tabKey}));
            check(!view->bandGenerationOpen() && !view->generationBar().isShowing(), "la fenêtre se ferme");
            check(depth() == laid->depth + 1, "une entrée d'historique");
            const auto* lead = leadOf(laid->fresh, laid->tracks[2]);
            check(lead != nullptr && lead->notes.size() == ghosts.size(),
                  "la rangée Lead du pattern neuf a les notes proposées");
            check(view->pickedNotes().size() == ghosts.size(), "les notes écrites sont les notes choisies");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->before, "Ctrl+Z : le projet à l'octet");
            static_cast<void>(view->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
        });

    add("S19 : retour sur SONG, le premier pattern choisi",
        [this, laid]
        {
            selection_.selectPattern(laid->pattern);
            press("SONG");
        });

    // S19: a track added to a pattern from the canvas, in song mode.
    add("S19 : un canal « Pad toile » au rack, joué dans aucun pattern",
        [this, laid]
        {
            laid->padDepth = depth();
            laid->pad = domain::TrackId::generate();
            check(bus_.execute(std::make_unique<domain::AddTrack>(laid->pad, "Pad toile", 0.0)).ok(),
                  "le canal est ajouté");
        });

    // A menu closes itself when the application is not in front: it is opened
    // and answered in the same step, as the rack's.
    add(
        "S19 : sous les bandes du premier bloc, « + piste », dessinée dans le bloc seul ; un clic, et « Pad "
        "toile » "
        "au menu des canaux absents",
        [this, canvas, laid]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            view->frameBlock(laid->placements.front());
            auto area = view->addBandIn(laid->placements.front());
            for (int notch = 0;
                 notch < 400 && area.has_value() && !view->timelineArea().contains(area->getCentre());
                 ++notch)
            {
                wheel(*view,
                      view->timelineArea().getCentre(),
                      area->getCentreY() >= view->timelineArea().getBottom() ? -0.1f : 0.1f);
                area = view->addBandIn(laid->placements.front());
            }
            check(area.has_value() && view->timelineArea().contains(area->getCentre()),
                  "la bande « + piste » est à l'écran");
            if (!area.has_value())
                return;

            // At the render: the band is painted inside the block, not on the
            // empty bar before it.
            const auto image = view->createComponentSnapshot(view->getLocalBounds(), false, 1.0f);
            const auto inside = juce::Point<int>{area->getRight() - 2, area->getCentreY()};
            const auto before = juce::Point<int>{area->getX() - 4, area->getCentreY()};
            check(view->timelineArea().contains(before), "la mesure vide avant le bloc est à l'écran");
            check(image.getPixelAt(inside.getX(), inside.getY()) !=
                      image.getPixelAt(before.getX(), before.getY()),
                  "au rendu, la bande est dessinée dans le bloc : " +
                      image.getPixelAt(inside.getX(), inside.getY()).toDisplayString(false).toStdString() +
                      " contre " +
                      image.getPixelAt(before.getX(), before.getY()).toDisplayString(false).toStdString());
            snapshot("s19-toile-plus-piste");

            laid->before = domain::json::write(state_.toValue());
            laid->depth = depth();
            click(*view, inside);

            // Every channel of the rack but the three of the line, in the
            // rack's order: the one just added comes last.
            chooseMenuItem(static_cast<int>(state_.tracks().size() - laid->tracks.size()));
        },
        [this, laid, leadOf] { return leadOf(laid->pattern, laid->pad) != nullptr; });

    add("S19 : le pattern a une rangée Pad, vide, une entrée ; Toile B n'a rien reçu ; Ctrl+Z à l'octet",
        [this, laid, leadOf]
        {
            const auto* row = leadOf(laid->pattern, laid->pad);
            check(row != nullptr && row->notes.empty(), "une rangée Pad, vide, dans le premier pattern");
            check(leadOf(laid->other, laid->pad) == nullptr, "Toile B n'a pas de rangée Pad");
            check(depth() == laid->depth + 1, "une entrée d'historique");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->before, "Ctrl+Z : le projet à l'octet");
        });

    add("S19 : en SONG, un clic dans la bande Kick de Toile B, qui n'a pas de Kick : la rangée s'ouvre",
        [this, canvas, laid, inSight]
        {
            auto* view = canvas();
            if (view == nullptr)
                return;
            view->frameBlock(laid->otherPlacement);
            const auto at = inSight(*view, laid->otherPlacement, laid->tracks[0], 1.0, 36);
            check(at.has_value() && view->timelineArea().contains(*at), "la bande Kick passe sur Toile B");
            if (!at.has_value())
                return;
            laid->before = domain::json::write(state_.toValue());
            laid->depth = depth();
            click(*view, *at);
        });

    add("S19 : Toile B a une rangée Kick et sa note, une entrée ; Ctrl+Z à l'octet ; le canal Pad retiré",
        [this, laid, leadOf]
        {
            const auto* row = leadOf(laid->other, laid->tracks[0]);
            check(row != nullptr && row->notes.size() == 1 && row->notes.front().pitch == 36 &&
                      std::abs(row->notes.front().startBeats - 1.0) < 1e-9,
                  "une rangée Kick dans Toile B, un do2 au temps 2");
            check(depth() == laid->depth + 1, "une entrée d'historique");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == laid->before, "Ctrl+Z : le projet à l'octet");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(state_.findTrack(laid->pad) == nullptr && depth() == laid->padDepth, "le canal Pad retiré");
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

            // The beatmaker opens on PAT, where the canvas shows one pattern
            // (S19): the song is what is measured.
            press("SONG");
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
            check(!view->showsPattern() && view->lanesShown() >= 32,
                  "la toile montre le morceau : 32 lignes");

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
