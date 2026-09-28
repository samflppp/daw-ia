#include "Verification.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/panels/PlaylistPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace daw::app
{

void Verification::addZoneSteps()
{
    const auto playlist = [this] { return dynamic_cast<ui::PlaylistPanel*>(panel("playlist")); };

    // The three lines, where the zone starts, and what the project was.
    struct Zone
    {
        std::vector<domain::LaneId> lanes;
        double fromBeats{0.0};
        std::string before;
        std::size_t depth{0};
        std::size_t patterns{0};
    };
    auto zone = std::make_shared<Zone>();

    add("S17 : trois lignes nommées Accords, Basse, Mélodie ; Alt + glisser dessine une zone de quatre "
        "mesures "
        "sur les trois",
        [this, playlist, zone]
        {
            if (clock_.isPlaying())
                static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            press("SONG");

            // Three channels named as a producer names them, and three lines
            // named after what should go on them. Through the bus: the rack
            // and the line menu are not what is verified here.
            for (const auto* name : {"Keys", "808", "Pluck"})
                static_cast<void>(
                    bus_.execute(std::make_unique<domain::AddTrack>(domain::TrackId::generate(), name, 0.0)));
            for (const auto* name : {"Accords", "Basse", "Mélodie"})
            {
                const auto lane = domain::LaneId::generate();
                static_cast<void>(bus_.execute(std::make_unique<domain::CreateLane>(
                    lane, juce::String::fromUTF8(name).toStdString(), state_.lanes().size())));
                zone->lanes.push_back(lane);
            }

            // After the song, on a whole bar.
            const auto bar = state_.beatsPerBar();
            double end = 0.0;
            for (const auto& placement : state_.arrangement())
            {
                if (const auto* pattern = state_.findPattern(placement.patternId); pattern != nullptr)
                    end = std::max(end, placement.startBeats + pattern->lengthBeats);
            }
            for (const auto& clip : state_.audioClips())
                end = std::max(end, clip.startBeats + 4.0);
            zone->fromBeats = std::ceil(end / bar) * bar + bar;
            zone->before = domain::json::write(state_.toValue());
            zone->depth = depth();
            zone->patterns = state_.patterns().size();

            auto* view = playlist();
            check(view != nullptr, "la playlist est là");
            if (view == nullptr)
                return;

            const auto first = static_cast<int>(state_.lanes().size()) - 3;
            const auto last = first + 2;
            static_cast<void>(playlistBeat(first, zone->fromBeats + 0.5));
            const auto from = playlistBeat(first, zone->fromBeats + 0.5);
            const auto to = playlistBeat(last, zone->fromBeats + 4.0 * bar - 0.5);
            drag(*view, from, to, false, false, false, true);

            check(view->hasZone(), "une zone est dessinée");
            check(domain::json::write(state_.toValue()) == zone->before && depth() == zone->depth,
                  "dessiner une zone n'écrit rien");
        });

    add("S17 : Ctrl+G, « boucle trap F#m », Entrée : accords, basse et mélodie, chacun sur sa ligne et sa "
        "piste, "
        "tous en Fa# mineur ; rien d'écrit",
        [this, playlist, zone]
        {
            auto* view = playlist();
            if (view == nullptr)
                return;

            static_cast<void>(view->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            check(view->generationBar().isShowing(), "la fenêtre s'ouvre sous la playlist");
            check(view->generationBar().getY() >= view->timelineArea().getBottom(),
                  "sous la grille, jamais sur les blocs");

            view->generationBar().field().setText(juce::String::fromUTF8("boucle trap F#m"), false);
            static_cast<void>(
                view->generationBar().field().keyPressed(juce::KeyPress{juce::KeyPress::returnKey}));

            const auto* proposal = view->zoneProposal();
            check(proposal != nullptr, "une proposition sur la zone");
            if (proposal == nullptr)
                return;

            check(proposal->parts().size() == 3,
                  "trois parties : " + std::to_string(proposal->parts().size()));
            const domain::generation::Key key{6, domain::generation::Mode::minor};
            auto inKey = true;
            auto onTheirLine = true;
            for (std::size_t index = 0; index < proposal->parts().size(); ++index)
            {
                const auto& part = proposal->parts()[index];
                const auto* track = state_.findTrack(part.track);
                note(std::string{domain::generation::describe(part.role)} + " sur " +
                     (track != nullptr ? track->name : std::string{"?"}));
                onTheirLine = onTheirLine && index < zone->lanes.size() && part.lane == zone->lanes[index];
                for (const auto& ghost : part.notes)
                    inKey = inKey && domain::generation::inScale(ghost.pitch, key);
            }
            check(onTheirLine, "chaque partie sur sa ligne");
            check(proposal->parts().size() == 3 &&
                      proposal->parts()[0].role == domain::generation::Role::chords &&
                      proposal->parts()[1].role == domain::generation::Role::bass &&
                      proposal->parts()[2].role == domain::generation::Role::melody,
                  "les rôles que les noms disent : accords, basse, mélodie");
            check(inKey, "toutes les notes en Fa# mineur");
            note("phrase : « " + view->generationBar().sentence().toStdString() + " »");
            check(domain::json::write(state_.toValue()) == zone->before && depth() == zone->depth,
                  "rien n'est écrit avant Tab");
            snapshot("s17-zone");
        });

    add("S17 : ▶ Écouter joue la proposition entière, et le projet ne bouge pas",
        [this, playlist, zone]
        {
            auto* view = playlist();
            if (view == nullptr || view->zoneProposal() == nullptr)
                return;

            static_cast<void>(view->keyPressed(
                juce::KeyPress{juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0}));
            const auto heard = listen("s17-zone-ecoute", state_.tempoAt(zone->fromBeats));
            const auto firstStep = static_cast<int>(std::lround(zone->fromBeats * 4.0));
            const auto inZone = std::count_if(heard.onsets.begin(),
                                              heard.onsets.end(),
                                              [firstStep](int step) { return step >= firstStep; });
            note("attaques dans la zone pendant l'écoute : " + std::to_string(inZone));
            check(inZone > 0, "la proposition s'entend dans la zone");
            check(domain::json::write(state_.toValue()) == zone->before && depth() == zone->depth,
                  "écouter n'écrit rien");
            static_cast<void>(view->keyPressed(
                juce::KeyPress{juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0}));
        });

    add("S17 : Tab écrit les trois parties en une seule entrée d'historique ; un Ctrl+Z les retire toutes",
        [this, playlist, zone]
        {
            auto* view = playlist();
            if (view == nullptr || view->zoneProposal() == nullptr)
                return;

            static_cast<void>(view->keyPressed(juce::KeyPress{juce::KeyPress::tabKey}));
            check(depth() == zone->depth + 1, "une seule entrée d'historique");
            check(state_.patterns().size() == zone->patterns + 3, "trois patterns de plus");

            auto laid = 0;
            for (const auto& placement : state_.arrangement())
            {
                if (std::find(zone->lanes.begin(), zone->lanes.end(), placement.laneId) !=
                        zone->lanes.end() &&
                    std::abs(placement.startBeats - zone->fromBeats) < 1e-9)
                    ++laid;
            }
            check(laid == 3, "un bloc sur chacune des trois lignes, au début de la zone");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == zone->before,
                  "Ctrl+Z : le projet d'avant, à l'octet");
        });
}

} // namespace daw::app
