#include "Verification.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/Phrase.h"
#include "daw/domain/generation/Transform.h"
#include "daw/domain/serialization/Json.h"
#include "daw/engine/PitchDetection.h"
#include "daw/engine/Rendering.h"
#include "daw/ui/panels/PianoRollPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace daw::app
{
namespace
{

using domain::generation::GhostNote;

[[nodiscard]] std::string keyName(const domain::generation::Key& key)
{
    return domain::generation::describe(key);
}

} // namespace

void Verification::addGenerationSteps()
{
    // --- S14: generation in the piano roll -------------------------------------------
    //
    // A channel with no sample, a range taken on the ruler with Shift + drag,
    // Ctrl+G, then everything the user can do to a proposal before keeping it:
    // type constraints, ask again, walk the variants, reject, change pattern,
    // undo something else, generate while the song plays. Until Tab, the
    // project and the history must not have moved by one byte. Tab writes one
    // group from the generator, and the render says where the attacks are and
    // which pitches sound.

    const auto roll = [this] { return dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll")); };
    const auto untouched = [this](const std::string& what)
    {
        check(domain::json::write(state_.toValue()) == generationBaseline_,
              what + " : le projet n'a pas bougé");
        check(depth() == generationDepth_, what + " : aucune entrée d'historique");
    };
    const auto ghostsLegal = [this, roll](const std::string& what)
    {
        auto* panel = roll();
        const auto* proposal = panel != nullptr ? panel->proposal() : nullptr;
        check(proposal != nullptr && !panel->ghostNotes().empty(), what + " : des notes proposées");
        if (proposal == nullptr)
            return;

        const auto key = proposal->constraints().key.value;
        auto inKey = true;
        auto inRange = true;
        for (const auto& ghost : panel->ghostNotes())
        {
            inKey = inKey && domain::generation::inScale(ghost.pitch, key);
            inRange = inRange && ghost.startBeats >= proposal->fromBeats() - 1e-9 &&
                      ghost.startBeats + ghost.lengthBeats <= proposal->toBeats() + 1e-9;
        }
        check(inKey, what + " : toutes en " + keyName(key));
        check(inRange, what + " : toutes dans la plage");
        check(panel->lastGenerationMs() < 16.0,
              what + " : en " + juce::String(panel->lastGenerationMs(), 2).toStdString() + " ms, sous 16 ms");
        note(panel->proposalLine().toStdString());
    };
    // The words as a person types them: UTF-8, never a char literal read as
    // Latin-1 by juce::String, which turned "mélodie" into a word nobody wrote.
    const auto enter = [this, roll](const juce::String& text)
    {
        if (auto* panel = roll(); panel != nullptr)
            prompt(panel->generationBar(), text);
    };

    add("un canal Lead sans sample ; au piano-roll, Maj + glisser sur la règle prend les mesures 2 et 3",
        [this, roll]
        {
            if (clock_.isPlaying())
                static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
            press("PAT");
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            // The channel is made through the bus: the rack is not what is
            // verified here.
            leadTrack_ = domain::TrackId::generate();
            static_cast<void>(bus_.execute(std::make_unique<domain::AddTrack>(leadTrack_, "Lead", 0.0)));
            selection_.selectPattern(state_.patterns().front().id);
            if (auto* rack = panel("channel_rack"); rack != nullptr)
                click(*rack, rackChannel(static_cast<int>(state_.tracks().size()) - 1));
            selection_.dispatchPendingMessages();
            check(selection_.track() == leadTrack_, "le canal Lead est choisi");

            if (auto* shown = panel("piano_roll"); shown == nullptr || !shown->isShowing())
                key(juce::KeyPress{juce::KeyPress::F7Key});
            auto* panel = roll();
            check(panel != nullptr && panel->isShowing(), "le piano-roll est devant");
            if (panel == nullptr)
                return;

            const auto bar = state_.beatsPerBar();
            const auto* pattern = state_.findPattern(selection_.pattern());
            const auto to = std::min(3.0 * bar, pattern != nullptr ? pattern->lengthBeats : 3.0 * bar);
            const auto y = panel->ruler().getCentreY();
            drag(*panel,
                 {panel->pointFor(bar, 60).getX(), y},
                 {panel->pointFor(to, 60).getX(), y},
                 false,
                 false,
                 true);

            const auto range = panel->range();
            check(range.has_value() && std::abs(range->first - bar) < 1e-9 &&
                      std::abs(range->second - to) < 1e-9,
                  "la plage va du temps " + juce::String(bar, 2).toStdString() + " au temps " +
                      juce::String(to, 2).toStdString());
            generationBaseline_ = domain::json::write(state_.toValue());
            generationDepth_ = depth();
            check(generationDepth_ == savedDepth_ + 1, "choisir une plage n'écrit rien");
        });

    add("S16 : Ctrl+G ouvre la fenêtre sous le piano-roll, hors de la grille, et ne propose rien avant le "
        "prompt",
        [this, roll, untouched, enter]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;
            check(panel->generateButton().isShowing(), "le bouton « Générer » est visible dans l'en-tête");
            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));

            check(panel->generationOpen(), "la fenêtre est ouverte");
            // The grid ends where the velocity lane begins: under the lane is
            // under the notes.
            const auto window = panel->generationBar().getBounds();
            check(window.getY() >= panel->velocityLane().getBottom() &&
                      window.getY() > panel->ruler().getBottom(),
                  "elle est sous la grille et sous les vélocités, jamais sur les notes");
            check(!panel->proposing() && panel->ghostNotes().empty(), "rien n'est proposé avant le prompt");

            enter({});
            check(!panel->proposing(), "Entrée sur un prompt vide : toujours rien");
            note("la fenêtre dit : « " + panel->generationBar().message().toStdString() + " »");
            untouched("ouvrir la fenêtre");
        });

    add("« propose-moi quelque chose », Entrée : des notes grises et une phrase courte ; la ligne technique "
        "est repliée",
        [this, roll, untouched, ghostsLegal, enter]
        {
            enter(juce::String::fromUTF8("propose-moi quelque chose"));
            auto* panel = roll();
            check(panel != nullptr && panel->proposing(), "une proposition est à l'écran");
            if (panel == nullptr || !panel->proposing())
                return;
            ghostsLegal("rien d'imposé");
            // Nothing harmonic in the pattern: the kicks and hats are one
            // pitch each. The key is the default, and says so.
            const auto* proposal = panel->proposal();
            check(proposal != nullptr &&
                      proposal->constraints().key.source != domain::generation::Source::deduced,
                  "aucune tonalité lue dans des lignes de batterie");

            const auto sentence = panel->proposalSentence();
            note("phrase : « " + sentence.toStdString() + " »");
            const auto length = juce::String::fromUTF8(
                domain::generation::lengthWords(
                    proposal != nullptr ? proposal->toBeats() - proposal->fromBeats() : 0.0,
                    state_.beatsPerBar())
                    .c_str());
            check(sentence.startsWith(length),
                  "la phrase dit la longueur : « " + length.toStdString() + " »");
            check(!sentence.contains(juce::String::fromUTF8("(déduit)")) && !sentence.contains("AABA") &&
                      !sentence.contains(juce::String::fromUTF8("variante")),
                  "la phrase ne parle pas la langue du moteur");
            check(!panel->generationBar().detailsOpen(), "les détails sont repliés par défaut");
            check(panel->proposalLine().contains(juce::String::fromUTF8("(déduit)")),
                  "les détails gardent ce que le moteur a choisi");
            untouched("proposer");
            snapshot("s16-fenetre");
        });

    add("« Am doubles dense grave basse sombre », Entrée : une basse en La mineur ; « sombre » n'est pas "
        "une retouche sur une zone vide",
        [this, roll, untouched, ghostsLegal, enter]
        {
            enter("Am doubles dense grave basse sombre");
            auto* panel = roll();
            const auto* proposal = panel != nullptr ? panel->proposal() : nullptr;
            check(proposal != nullptr, "une proposition");
            if (proposal == nullptr)
                return;

            const auto& constraints = proposal->constraints();
            check(constraints.key.value == domain::generation::Key{9, domain::generation::Mode::minor} &&
                      constraints.key.source == domain::generation::Source::imposed,
                  "La mineur, imposé");
            check(constraints.role.value == domain::generation::Role::bass, "une basse");
            check(constraints.reg.value == domain::generation::Register::low, "grave");
            // Read by the copilot's model, « sombre » may be translated
            // (minor, low) or said ignored: the model's call, and the line
            // says which. Read by the local words, it is said ignored — a
            // unit test pins it (PromptReaderTests). Never a retouche: the
            // zone has nothing to rework.
            const auto line = panel->proposalLine();
            note("ligne : " + line.toStdString());
            check(!line.contains(juce::String::fromUTF8("retouche")), "pas de retouche sur une zone vide");

            const auto [low, high] = domain::generation::registerRange(domain::generation::Role::bass,
                                                                       domain::generation::Register::low);
            auto inRegister = true;
            for (const auto& ghost : panel->ghostNotes())
                inRegister = inRegister && ghost.pitch >= low && ghost.pitch <= high;
            check(inRegister, "toutes dans le registre grave de la basse");
            ghostsLegal("basse");
            untouched("régénérer");
            firstGhosts_ = panel->ghostNotes();
            snapshot("s14-basse");
        });

    add("Entrée sur les mêmes mots : une autre variante ; Alt + molette revient à la première",
        [this, roll, untouched, enter]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;

            enter(panel->promptField().getText());
            check(panel->variantRank() == 1, "variante 2");
            check(panel->ghostNotes() != firstGhosts_, "d'autres notes");

            wheel(*panel, panel->pointFor(1.0, 40), 0.25f, false, false, true);
            check(panel->variantRank() == 0, "Alt + molette vers le haut : variante 1");
            check(panel->ghostNotes() == firstGhosts_, "les notes de la variante 1, les mêmes qu'avant");

            wheel(*panel, panel->pointFor(1.0, 40), -0.25f, false, false, true);
            check(panel->variantRank() == 1, "Alt + molette vers le bas : variante 2");
            untouched("parcourir les variantes");
        });

    // --- S16: listening before writing. The effect is measured on the Lead's
    // own meter, the state and the history are compared to the byte.
    add(
        "S16 : ▶ Écouter : la basse proposée joue en boucle sur le Lead, sans rien écrire",
        [roll]
        {
            if (auto* panel = roll(); panel != nullptr)
                panel->generationBar().listenButton().triggerClick();
        },
        [this, roll]
        {
            auto* panel = roll();
            return panel != nullptr && panel->listeningToProposal() && clock_.isPlaying() &&
                   levelOf(leadTrack_.toString()).peakDb > -60.0f;
        },
        6000.0);

    add("pendant l'écoute : le Lead sonne, le projet et l'historique n'ont pas bougé ; une autre variante "
        "s'entend aussitôt",
        [this, roll, untouched, enter]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;
            const auto lead = levelOf(leadTrack_.toString());
            note("Lead pendant l'écoute : crête " + juce::String(lead.peakDb, 1).toStdString() + " dBFS");
            check(lead.peakDb > -60.0f, "le Lead sonne : ce sont les notes grises, il n'en a aucune écrite");
            check(panel->generationBar().isListening(), "le bouton dit « Arrêter »");
            untouched("écouter");

            const auto before = panel->ghostNotes();
            enter(panel->promptField().getText());
            check(panel->ghostNotes() != before, "une autre variante");
            check(panel->listeningToProposal() && clock_.isPlaying(),
                  "l'écoute continue, sur la nouvelle variante");
            untouched("changer de variante en écoutant");
        });

    add("Ctrl+Espace arrête l'écoute : le transport revient où le domaine le dit",
        [this, roll, untouched]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;
            static_cast<void>(panel->keyPressed(
                juce::KeyPress{juce::KeyPress::spaceKey, juce::ModifierKeys::ctrlModifier, 0}));
            check(!panel->listeningToProposal() && !panel->generationBar().isListening(),
                  "l'écoute est arrêtée");
            check(!clock_.isPlaying(), "à l'arrêt, comme le domaine");
            check(panel->proposing(), "la proposition est toujours là");
            untouched("arrêter l'écoute");
        });

    add("désordre : Échap rejette, puis Ctrl+G deux fois de suite sans accepter",
        [this, roll, untouched, enter]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;

            static_cast<void>(panel->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
            check(!panel->proposing() && !panel->generationOpen(),
                  "Échap ferme la fenêtre et efface le gris");
            untouched("rejeter");

            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            check(panel->generationOpen() && !panel->proposing(), "une fenêtre, et rien de proposé");
            enter("Am doubles dense grave basse");
            check(panel->proposing(), "la basse est de nouveau proposée");
            untouched("générer deux fois");
        });

    add("désordre : un fader bougé puis Ctrl+Z pendant la proposition",
        [this, roll, untouched]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;
            const auto before = panel->ghostNotes();

            const auto other = state_.tracks().front().id;
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackVolume>(other, -9.0)));
            check(depth() == generationDepth_ + 1, "le fader fait une entrée");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            selection_.dispatchPendingMessages();

            check(panel->proposing(), "la proposition est toujours là");
            check(panel->ghostNotes() == before, "les mêmes notes : le mixage n'est pas leur contexte");
            untouched("annuler autre chose");
        });

    add("désordre : changer de pattern ferme la proposition, sans rien écrire",
        [this, roll, untouched, enter]
        {
            auto* panel = roll();
            if (panel == nullptr || state_.patterns().size() < 2)
            {
                note("un seul pattern : cas sauté");
                return;
            }

            const auto first = state_.patterns().front().id;
            selection_.selectPattern(state_.patterns()[1].id);
            selection_.dispatchPendingMessages();
            check(!panel->proposing(), "la proposition est fermée avec l'autre pattern");
            selection_.selectPattern(first);
            selection_.dispatchPendingMessages();
            check(!panel->proposing(), "elle ne revient pas toute seule");
            untouched("changer de pattern");

            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            enter("Am doubles dense grave basse");
        });

    add(
        "désordre : pendant la lecture, Entrée régénère et la lecture continue",
        [this] { static_cast<void>(bus_.execute(std::make_unique<domain::TransportPlay>())); },
        [this] { return clock_.isPlaying() && clock_.positionBeats() > 0.5; },
        8000.0);

    add("pendant la lecture : « Am croches mélodie », Entrée",
        [this, roll, untouched, ghostsLegal, enter]
        {
            enter(juce::String::fromUTF8("Am croches mélodie"));
            auto* panel = roll();
            check(clock_.isPlaying(), "la lecture continue");
            check(panel != nullptr && panel->proposing(), "une mélodie est proposée");
            const auto* proposal = panel != nullptr ? panel->proposal() : nullptr;
            check(proposal != nullptr && proposal->interpretation().ignored.empty() &&
                      proposal->constraints().role.source == domain::generation::Source::imposed,
                  "« mélodie » est compris, rien d'ignoré");
            auto onEighths = true;
            if (panel != nullptr)
            {
                for (const auto& ghost : panel->ghostNotes())
                    onEighths = onEighths &&
                                std::abs(ghost.startBeats * 2.0 - std::round(ghost.startBeats * 2.0)) < 1e-9;
            }
            check(onEighths, "toutes les attaques sur des croches");
            ghostsLegal("pendant la lecture");
            untouched("générer pendant la lecture");
        });

    add("Tab, toujours pendant la lecture : un seul groupe, marqué générateur, et les notes grises écrites",
        [this, roll]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;
            acceptedGhosts_ = panel->ghostNotes();
            static_cast<void>(panel->keyPressed(juce::KeyPress{juce::KeyPress::tabKey}));

            check(!panel->proposing() && !panel->generationOpen(), "la fenêtre se ferme");
            check(depth() == generationDepth_ + 1, "une seule entrée d'historique");
            const auto& entries = history_.entries();
            const auto* last = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(last != nullptr && last->actor == domain::Actor::generator, "marquée générateur");
            if (last != nullptr)
                note("libellé : « " + std::string{last->label()} + " »");

            const auto* pattern = state_.findPattern(selection_.pattern());
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(leadTrack_) : nullptr;
            check(row != nullptr, "la ligne du Lead est ouverte dans le même groupe");
            if (row == nullptr)
                return;

            auto written = row->notes;
            std::sort(written.begin(),
                      written.end(),
                      [](const domain::Note& a, const domain::Note& b)
                      { return a.startBeats < b.startBeats; });
            auto same = written.size() == acceptedGhosts_.size();
            for (std::size_t i = 0; same && i < written.size(); ++i)
            {
                same = written[i].pitch == acceptedGhosts_[i].pitch &&
                       std::abs(written[i].startBeats - acceptedGhosts_[i].startBeats) < 1e-9 &&
                       std::abs(written[i].lengthBeats - acceptedGhosts_[i].lengthBeats) < 1e-9 &&
                       written[i].velocity == acceptedGhosts_[i].velocity;
            }
            check(same,
                  "ce qui est écrit est ce qui était gris : " + std::to_string(written.size()) + " notes");
            check(clock_.isPlaying(), "la lecture continue");
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
        });

    add("au rendu, le Lead seul : chaque attaque à sa place, chaque hauteur en La mineur",
        [this]
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackSolo>(leadTrack_, true)));
            const auto heard = render("s14-generation-lead");
            static_cast<void>(bus_.undo());
            if (heard.sampleRate <= 0.0)
                return;

            // In pattern mode the pattern plays from the start of the Edit.
            const auto sampleAt = [this, &heard](double beats)
            {
                return static_cast<int>(std::lround(
                    edit_.tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats)).inSeconds() *
                    heard.sampleRate));
            };
            const auto& audio = heard.audio;

            // Per sixteenth of the pattern: level and pitch.
            const auto* pattern = state_.findPattern(selection_.pattern());
            const auto steps =
                pattern != nullptr
                    ? static_cast<int>(std::lround(pattern->lengthBeats / domain::generation::stepBeats))
                    : 0;
            std::vector<float> levels;
            std::vector<int> pitches;
            for (int step = 0; step < steps; ++step)
            {
                const auto start = sampleAt(step * domain::generation::stepBeats);
                const auto end =
                    std::min(sampleAt((step + 1) * domain::generation::stepBeats), audio.getNumSamples());
                if (end <= start)
                    break;
                levels.push_back(audio.getRMSLevel(0, start, end - start));
                const auto skip = static_cast<int>(0.012 * heard.sampleRate);
                const auto hertz = end - start > skip * 2
                                       ? engine::fundamentalOf(audio.getReadPointer(0, start + skip),
                                                               end - start - skip,
                                                               heard.sampleRate,
                                                               30.0,
                                                               1500.0)
                                       : 0.0;
                pitches.push_back(hertz > 0.0 ? engine::midiPitchOf(hertz) : -1);
            }

            std::vector<int> onsets;
            const auto loudest = levels.empty() ? 0.0f : *std::max_element(levels.begin(), levels.end());
            for (std::size_t step = 0; step < levels.size(); ++step)
            {
                const auto before = step == 0 ? 0.0f : levels[step - 1];
                const auto moved = step > 0 && pitches[step] >= 0 && pitches[step - 1] >= 0 &&
                                   pitches[step] % 12 != pitches[step - 1] % 12;
                if (levels[step] > loudest * 0.05f && (levels[step] > before * 1.25f || moved))
                    onsets.push_back(static_cast<int>(step));
            }

            std::set<int> starts;
            std::set<int> audible;
            for (std::size_t i = 0; i < acceptedGhosts_.size(); ++i)
            {
                const auto step = static_cast<int>(
                    std::lround(acceptedGhosts_[i].startBeats / domain::generation::stepBeats));
                starts.insert(step);
                const auto tied = i > 0 &&
                                  acceptedGhosts_[i - 1].pitch % 12 == acceptedGhosts_[i].pitch % 12 &&
                                  acceptedGhosts_[i - 1].startBeats + acceptedGhosts_[i - 1].lengthBeats >=
                                      acceptedGhosts_[i].startBeats - 1e-6;
                if (!tied)
                    audible.insert(step);
            }

            std::string heardAt;
            int offNote = 0;
            for (const auto onset : onsets)
            {
                heardAt += std::to_string(onset) + " ";
                offNote += starts.count(onset) == 0 ? 1 : 0;
            }
            int missed = 0;
            for (const auto start : audible)
                missed += std::find(onsets.begin(), onsets.end(), start) == onsets.end() ? 1 : 0;
            note("attaques entendues (double-croches) : " + heardAt);
            check(offNote == 0, "aucune attaque là où aucune note ne commence");
            check(missed == 0,
                  "chacune des " + std::to_string(audible.size()) + " attaques audibles est entendue");

            const domain::generation::Key aMinor{9, domain::generation::Mode::minor};
            int measured = 0;
            int outOfKey = 0;
            int wrong = 0;
            for (const auto& ghost : acceptedGhosts_)
            {
                const auto step =
                    static_cast<std::size_t>(std::lround(ghost.startBeats / domain::generation::stepBeats));
                if (step >= pitches.size() || pitches[step] < 0)
                    continue;
                ++measured;
                outOfKey += domain::generation::inScale(pitches[step], aMinor) ? 0 : 1;
                wrong += pitches[step] % 12 == ghost.pitch % 12 ? 0 : 1;
            }
            note(std::to_string(measured) + " hauteurs mesurées sur " +
                 std::to_string(acceptedGhosts_.size()));
            check(measured * 10 >= static_cast<int>(acceptedGhosts_.size()) * 9,
                  "au moins neuf sur dix mesurées");
            check(outOfKey == 0, "aucune hauteur entendue hors de La mineur");
            check(wrong == 0, "chaque note sonne à la hauteur écrite");
        });

    // --- S16: reworking notes that exist. The notes just written are the zone;
    // what each prompt keeps and changes is measured on the notes, before and
    // after, and nothing is written before Tab.
    const auto reworkBase = std::make_shared<std::string>();
    const auto reworkDepth = std::make_shared<std::size_t>(0);
    const auto rhythmOf = [](const std::vector<domain::generation::GhostNote>& notes)
    {
        std::vector<std::tuple<long long, long long, int>> out;
        for (const auto& ghost : notes)
            out.emplace_back(std::llround(ghost.startBeats * 960.0),
                             std::llround(ghost.lengthBeats * 960.0),
                             ghost.velocity);
        std::sort(out.begin(), out.end());
        return out;
    };
    const auto pitchesOf = [](std::vector<domain::generation::GhostNote> notes)
    {
        std::stable_sort(notes.begin(),
                         notes.end(),
                         [](const auto& lhs, const auto& rhs) { return lhs.startBeats < rhs.startBeats; });
        std::vector<int> out;
        for (const auto& ghost : notes)
            out.push_back(ghost.pitch);
        return out;
    };
    const auto reworkUntouched = [this, reworkBase, reworkDepth](const std::string& what)
    {
        check(domain::json::write(state_.toValue()) == *reworkBase, what + " : le projet n'a pas bougé");
        check(depth() == *reworkDepth, what + " : aucune entrée d'historique");
    };

    add("S16 retouche : les notes écrites dans la plage, Ctrl+G, « garde le rythme, change les notes »",
        [this, roll, enter, reworkBase, reworkDepth, reworkUntouched, rhythmOf, pitchesOf]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;
            *reworkBase = domain::json::write(state_.toValue());
            *reworkDepth = depth();

            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            enter(juce::String::fromUTF8("garde le rythme, change les notes"));
            const auto* rework = panel->rework();
            check(rework != nullptr && panel->proposal() == nullptr,
                  "les notes de la zone sont reprises, pas écrasées");
            if (rework == nullptr)
                return;

            const auto& before = rework->source();
            const auto& after = panel->ghostNotes();
            check(before.size() == acceptedGhosts_.size(), "la source : les notes écrites par Tab");
            check(rhythmOf(after) == rhythmOf(before), "mêmes attaques, mêmes durées, mêmes vélocités");
            const auto was = pitchesOf(before);
            const auto now = pitchesOf(after);
            std::size_t changed = 0;
            for (std::size_t i = 0; i < std::min(was.size(), now.size()); ++i)
                changed += was[i] != now[i] ? 1U : 0U;
            note(std::to_string(changed) + " hauteurs changées sur " + std::to_string(now.size()));
            check(changed * 2 >= now.size(), "au moins la moitié des hauteurs changent");
            const domain::generation::Key aMinor{9, domain::generation::Mode::minor};
            check(std::all_of(after.begin(),
                              after.end(),
                              [aMinor](const auto& ghost)
                              { return domain::generation::inScale(ghost.pitch, aMinor); }),
                  "toutes en La mineur");
            note("phrase : « " + panel->proposalSentence().toStdString() + " »");
            check(panel->proposalSentence() == juce::String::fromUTF8("même rythme, d'autres notes"),
                  "la phrase dit ce qui change");
            reworkUntouched("retoucher");
            snapshot("s16-retouche");
        });

    add("« plus sombre » : même rythme, plus bas ; « humanise » : mêmes hauteurs, placements à 1/64 près",
        [roll, enter, reworkUntouched, rhythmOf, pitchesOf, this]
        {
            auto* panel = roll();
            if (panel == nullptr)
                return;

            enter(juce::String::fromUTF8("plus sombre"));
            const auto* rework = panel->rework();
            check(rework != nullptr && rework->transform() == domain::generation::Transform::darker,
                  "« plus sombre » est compris");
            if (rework == nullptr)
                return;
            const auto mean = [](const auto& notes)
            {
                double sum = 0.0;
                for (const auto& ghost : notes)
                    sum += ghost.pitch;
                return notes.empty() ? 0.0 : sum / static_cast<double>(notes.size());
            };
            check(rhythmOf(panel->ghostNotes()) == rhythmOf(rework->source()),
                  "sombre : le rythme est gardé");
            check(mean(panel->ghostNotes()) < mean(rework->source()), "sombre : les hauteurs descendent");

            enter(juce::String::fromUTF8("humanise"));
            rework = panel->rework();
            check(rework != nullptr && rework->transform() == domain::generation::Transform::humanize,
                  "« humanise » est compris");
            if (rework == nullptr)
                return;
            check(pitchesOf(panel->ghostNotes()) == pitchesOf(rework->source()),
                  "humanise : les hauteurs sont gardées");
            auto widest = 0.0;
            auto source = rework->source();
            auto moved = panel->ghostNotes();
            for (std::size_t i = 0; i < std::min(source.size(), moved.size()); ++i)
                widest = std::max(widest, std::abs(source[i].startBeats - moved[i].startBeats));
            note("plus grand décalage : " + juce::String(widest, 4).toStdString() + " temps");
            check(widest > 0.0 && widest <= 1.0 / 64.0 + 1e-9,
                  "humanise : chaque attaque bouge d'1/64 de temps au plus");
            reworkUntouched("retoucher encore");
        });

    add("Tab : la retouche est une entrée « retouche », du générateur ; Ctrl+Z rend les notes d'avant",
        [this, roll, reworkBase, reworkDepth]
        {
            auto* panel = roll();
            if (panel == nullptr || panel->rework() == nullptr)
                return;
            const auto shown = panel->ghostNotes();
            static_cast<void>(panel->keyPressed(juce::KeyPress{juce::KeyPress::tabKey}));

            check(depth() == *reworkDepth + 1, "une seule entrée d'historique");
            const auto& entries = history_.entries();
            const auto* last = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(last != nullptr && last->actor == domain::Actor::generator, "marquée générateur");
            if (last != nullptr)
                note("libellé : « " + std::string{last->label()} + " »");

            const auto* pattern = state_.findPattern(selection_.pattern());
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(leadTrack_) : nullptr;
            check(row != nullptr && row->notes.size() == shown.size(),
                  "ce qui est écrit est ce qui était gris");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == *reworkBase,
                  "Ctrl+Z : les notes d'avant, à l'octet près");
        });

    add("Ctrl+Z défait la génération d'un coup ; tout défaire",
        [this]
        {
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == generationBaseline_,
                  "un Ctrl+Z : le projet d'avant Tab, à l'octet près");
            while (depth() > savedDepth_)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_,
                  "tout défait : le projet d'avant, à l'octet près");
            key(juce::KeyPress{juce::KeyPress::F7Key});
            press("SONG");
        });
}

} // namespace daw::app
