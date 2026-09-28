#include "Verification.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/Learning.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/TitleBarView.h"
#include "daw/ui/model/CopilotHost.h"
#include "daw/ui/model/PatternEditing.h"
#include "daw/ui/model/StyleLearning.h"
#include "daw/ui/model/StyleSource.h"
#include "daw/ui/panels/PianoRollPanel.h"

#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace daw::app
{
namespace
{

// The files of what was learned, the settings aside.
[[nodiscard]] juce::Array<juce::File> learnedFiles(const juce::File& folder)
{
    juce::Array<juce::File> out;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        if (file.getFileName() != "reglages.json")
            out.add(file);
    }
    return out;
}

// The share of onsets two sixteenths apart over sixteen proposals of four
// bars on an empty lead row in A minor: how much a style is in eighths.
[[nodiscard]] double eighthsOf(const domain::generation::Context& empty,
                               const domain::generation::StyleModel& model)
{
    domain::generation::Constraints wanted{};
    wanted.key = domain::generation::Key{9, domain::generation::Mode::minor};
    wanted.role = domain::generation::Role::melody;
    wanted.resolution = domain::generation::Resolution::sixteenth;
    const auto resolved = domain::generation::resolve(wanted, empty);

    double eighths = 0.0;
    double gaps = 0.0;
    for (int variant = 0; variant < 16; ++variant)
    {
        const auto notes = domain::generation::generate(empty, resolved, model, variant);
        for (std::size_t i = 1; i < notes.size(); ++i)
        {
            eighths += std::lround((notes[i].startBeats - notes[i - 1].startBeats) * 4.0) == 2 ? 1.0 : 0.0;
            gaps += 1.0;
        }
    }
    return gaps > 0.0 ? eighths / gaps : 0.0;
}

} // namespace

void Verification::addLearningSteps()
{
    // --- S15: the generator learns from the person -------------------------------
    //
    // In the verification's own folder, never the person's. The base first;
    // notes the generator wrote teach nothing; thirty-two eighths the person
    // writes are learned at the next Ctrl+G, and change what is proposed;
    // a save writes them off the message thread; the File menu turns the
    // project or all learning off; another project adds up; forgetting
    // deletes the files.

    const auto roll = [this] { return dynamic_cast<ui::PianoRollPanel*>(panel("piano_roll")); };
    const auto enter = [roll](const juce::String& text)
    {
        if (auto* panel = roll(); panel != nullptr)
        {
            panel->promptField().setText(text, false);
            static_cast<void>(panel->promptField().keyPressed(juce::KeyPress{juce::KeyPress::returnKey}));
        }
    };
    // Asks again with words that differ from the last ones: a new proposal,
    // drawn with the style as it is now.
    const auto ask = [roll, enter]
    {
        auto* panel = roll();
        if (panel == nullptr)
            return juce::String{};
        const auto words = panel->promptField().getText() == juce::String::fromUTF8("Am mélodie")
                               ? juce::String::fromUTF8("mélodie Am")
                               : juce::String::fromUTF8("Am mélodie");
        enter(words);
        return panel->proposalLine();
    };
    const auto lineSays = [this](const juce::String& line, const char* words, const std::string& what)
    {
        note(line.toStdString());
        check(line.contains(juce::String::fromUTF8(words)),
              what + " : « " + juce::String::fromUTF8(words).toStdString() + " »");
    };

    add("S15 apprentissage : un dossier à la vérification ; « Lead A » vide, Ctrl+G sur quatre mesures : le "
        "repli",
        [this, roll, lineSays]
        {
            auto* learning = ui::styleLearning();
            check(learning != nullptr, "l'apprentissage est en place");
            if (learning == nullptr)
                return;
            check(learning->folder().isAChildOf(folder_),
                  "ses fichiers vont dans le dossier de la vérification : " +
                      learning->folder().getFullPathName().toStdString());
            learning->forget();
            check(learnedFiles(learning->folder()).isEmpty(), "rien d'appris au départ");
            check(learning->enabled() && !learning->projectExcluded(), "apprendre : oui, ce projet compris");

            press("PAT");
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();

            leadTrack_ = domain::TrackId::generate();
            static_cast<void>(bus_.execute(std::make_unique<domain::AddTrack>(leadTrack_, "Lead A", 0.0)));
            selection_.selectPattern(state_.patterns().front().id);
            if (auto* rack = panel("channel_rack"); rack != nullptr)
                click(*rack, rackChannel(static_cast<int>(state_.tracks().size()) - 1));
            selection_.dispatchPendingMessages();

            if (auto* shown = panel("piano_roll"); shown == nullptr || !shown->isShowing())
                key(juce::KeyPress{juce::KeyPress::F7Key});
            auto* panel = roll();
            const auto bar = state_.beatsPerBar();
            if (panel == nullptr)
                return;
            const auto y = panel->ruler().getCentreY();
            drag(*panel,
                 {panel->pointFor(0.0, 60).getX(), y},
                 {panel->pointFor(4.0 * bar, 60).getX(), y},
                 false,
                 false,
                 true);

            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            panel->promptField().setText(juce::String::fromUTF8("Am mélodie"), false);
            static_cast<void>(panel->promptField().keyPressed(juce::KeyPress{juce::KeyPress::returnKey}));
            lineSays(panel->proposalLine(), "style : repli", "rien d'appris pour une mélodie : la ligne dit");
        });

    add("Tab, puis Ctrl+G : les notes écrites par le générateur ne lui apprennent rien",
        [this, roll, ask, lineSays]
        {
            auto* panel = roll();
            if (panel == nullptr || !panel->proposing())
                return;
            static_cast<void>(panel->keyPressed(juce::KeyPress{juce::KeyPress::tabKey}));
            const auto* pattern = state_.findPattern(selection_.pattern());
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(leadTrack_) : nullptr;
            check(row != nullptr && !row->notes.empty(),
                  "la ligne du Lead A porte " + std::to_string(row != nullptr ? row->notes.size() : 0) +
                      " notes du générateur");

            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            lineSays(ask(), "style : repli", "après Tab, la ligne dit encore");
        });

    add("la personne écrit 32 croches conjointes sur « Lead B » : la proposition suivante s'en sert, et le "
        "mesure",
        [this, roll, ask, lineSays]
        {
            auto* panel = roll();
            auto* learning = ui::styleLearning();
            if (panel == nullptr || learning == nullptr)
                return;

            const auto other = domain::TrackId::generate();
            static_cast<void>(bus_.execute(std::make_unique<domain::AddTrack>(other, "Lead B", 0.0)));
            const auto patternId = selection_.pattern();
            auto row = ui::patternEditing::rowFor(state_, patternId, other);
            auto commands = std::move(row.opening);
            const domain::generation::Key aMinor{9, domain::generation::Mode::minor};
            const auto home = *domain::generation::diatonicIndex(69, aMinor);
            const int steps[8] = {0, 1, 2, 3, 4, 3, 2, 1};
            for (int i = 0; i < 32; ++i)
            {
                domain::Note written{};
                written.id = domain::NoteId::generate();
                written.pitch = domain::generation::pitchOfIndex(home + steps[i % 8], aMinor);
                written.velocity = i % 2 == 0 ? 105 : 85;
                written.startBeats = i * 0.5;
                written.lengthBeats = 0.5;
                commands.push_back(std::make_unique<domain::AddNote>(row.clipId, written));
            }
            domain::GroupOptions group{};
            group.label = "32 croches";
            check(bus_.executeGroup(std::move(commands), group).ok(), "32 notes écrites par la personne");

            static_cast<void>(panel->keyPressed(juce::KeyPress{'g', juce::ModifierKeys::ctrlModifier, 0}));
            lineSays(ask(), "appris (1 projet, 32 notes)", "la ligne dit d'où vient le style");
            note("phrase : « " + panel->proposalSentence().toStdString() + " »");
            check(panel->proposalSentence().contains(juce::String::fromUTF8("dans ton style")),
                  "la phrase dit « dans ton style »");
            check(panel->lastStyleMs() < 16.0,
                  "le style est bâti en " + juce::String(panel->lastStyleMs(), 2).toStdString() +
                      " ms, sous 16 ms");

            // The effect, not the label: the same empty row, the base and the
            // style now, sixteen proposals each.
            auto context = domain::generation::Context::of(
                state_, patternId, leadTrack_, 0.0, 4.0 * state_.beatsPerBar());
            if (!context)
                return;
            auto empty = std::move(context).value();
            empty.row.clear();
            const auto before = eighthsOf(empty, ui::styleModel());
            const auto now = eighthsOf(empty, *learning->model(state_, ui::styleModel()));
            note("croches dans les propositions : repli " + juce::String(before * 100.0, 1).toStdString() +
                 " %, après 32 croches écrites " + juce::String(now * 100.0, 1).toStdString() + " %");
            check(now > before + 0.15, "les propositions penchent vers les croches");
        });

    add("Enregistrer : le projet est compté hors du fil des messages, et écrit dans son fichier",
        [this]
        {
            auto* learning = ui::styleLearning();
            if (learning == nullptr)
                return;
            titleBar_.runMenuItem(ui::TitleBarView::saveItem);
            learning->settle();
            const auto files = learnedFiles(learning->folder());
            check(files.size() == 1, "un fichier, celui de ce projet");
            if (files.isEmpty())
                return;
            auto value = domain::json::read(files.getFirst().loadFileAsString().toStdString());
            const auto* learnedValue = value ? value.value().find("learned") : nullptr;
            auto learned = learnedValue != nullptr ? domain::generation::Learned::fromValue(*learnedValue)
                                                   : domain::Result<domain::generation::Learned>{domain::fail(
                                                         domain::ErrorCode::invalidPayload, "vide")};
            check(learned.ok() && learned.value().role(domain::generation::Role::melody).notes == 32.0,
                  "il compte les 32 notes de la personne, et aucune du générateur");
        });

    add("Fichier > Génération : « Apprendre de ce projet », puis « Apprendre de mes projets », décochés",
        [this, ask, lineSays]
        {
            auto* learning = ui::styleLearning();
            if (learning == nullptr)
                return;

            titleBar_.runMenuItem(ui::TitleBarView::projectLearnItem);
            check(learning->projectExcluded(), "ce projet est exclu");
            lineSays(ask(), "repli (projet exclu)", "la ligne dit");
            const auto files = learnedFiles(learning->folder());
            auto kept =
                files.size() == 1
                    ? domain::json::read(files.getFirst().loadFileAsString().toStdString())
                    : domain::Result<domain::Value>{domain::fail(domain::ErrorCode::invalidPayload, "aucun")};
            const auto* excluded = kept ? kept.value().find("excluded") : nullptr;
            const auto* learned = kept ? kept.value().find("learned") : nullptr;
            check(excluded != nullptr && excluded->asBool() && excluded->asBool().value() &&
                      learned != nullptr && learned->isNull(),
                  "son fichier ne garde que l'exclusion, sans aucun compte");
            titleBar_.runMenuItem(ui::TitleBarView::projectLearnItem);
            lineSays(ask(), "appris (1 projet, 32 notes)", "recoché, la ligne dit");

            titleBar_.runMenuItem(ui::TitleBarView::learnItem);
            check(!learning->enabled(), "l'apprentissage est coupé");
            lineSays(ask(), "apprentissage coupé", "la ligne dit");
            titleBar_.runMenuItem(ui::TitleBarView::learnItem);
            check(learning->enabled(), "l'apprentissage reprend");
        });

    add("un autre projet déjà appris s'ajoute : « 2 projets »",
        [this, ask, lineSays]
        {
            auto* learning = ui::styleLearning();
            if (learning == nullptr)
                return;
            titleBar_.runMenuItem(ui::TitleBarView::saveItem);
            learning->settle();
            const auto files = learnedFiles(learning->folder());
            check(!files.isEmpty(), "le fichier de ce projet");
            if (files.isEmpty())
                return;
            // What another project would have left: the same counts, under
            // another name.
            check(files.getFirst().copyFileTo(learning->folder().getChildFile("autre-projet.json")),
                  "un second fichier, d'un autre projet");
            learning->reload();
            learning->settle();
            lineSays(ask(), "appris (2 projets, 64 notes)", "la ligne dit");
        });

    add("Oublier ce qui a été appris : les fichiers partent ; le projet ouvert apprend encore ; tout défaire",
        [this, roll, ask, lineSays]
        {
            auto* learning = ui::styleLearning();
            auto* panel = roll();
            if (learning == nullptr || panel == nullptr)
                return;
            titleBar_.runMenuItem(ui::TitleBarView::forgetItem);
            check(learnedFiles(learning->folder()).isEmpty(), "plus aucun fichier appris");
            check(learning->otherProjects() == 0, "plus aucun autre projet");
            check(titleBar_.status() == juce::String::fromUTF8("appris : oublié"),
                  "la barre dit « appris : oublié »");
            lineSays(ask(), "appris (1 projet, 32 notes)", "le projet ouvert, lui, est encore là");

            static_cast<void>(panel->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
            while (depth() > savedDepth_)
                key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_, "tout défait : le projet d'avant");
            key(juce::KeyPress{juce::KeyPress::F7Key});
            press("SONG");
        });

    // --- the copilot writing music, with the real model ---------------------------
    //
    // The request that failed on 27 September, word for word: the model used
    // to write every chord note by note and ran out of room. It now loads the
    // plugin, sets the tempo, opens a row and calls pattern.generate once.

    add(
        "S15 copilote : « ouvre un omnisphere et cree des accords triste dans un pattern en 140 bpm »",
        [this]
        {
            savedState_ = domain::json::write(state_.toValue());
            savedDepth_ = depth();
            transcriptBefore_ = copilot_.transcript().size();
            copilot_.ask("ouvre un omnisphere et cree des accords triste dans un pattern en 140 bpm");
        },
        [this]
        {
            return copilot_.transcript().size() > transcriptBefore_ + 1 &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        180000.0);

    add("ce qu'il a fait : Omnisphere, 140 BPM, des triades mineures, une seule entrée ; Ctrl+Z défait tout",
        [this]
        {
            const auto& lines = copilot_.transcript();
            const auto* last = lines.empty() ? nullptr : &lines.back();
            note("réponse : « " + (last != nullptr ? last->text : std::string{}) + " »");
            check(last != nullptr && last->from == ui::CopilotHost::Line::From::copilot,
                  "le copilote répond, sans refus");
            check(depth() == savedDepth_ + 1, "une seule entrée d'historique");
            const auto& entries = history_.entries();
            const auto* entry = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(entry != nullptr && entry->actor == domain::Actor::copilot, "marquée copilote");

            auto at140 = false;
            for (const auto& point : state_.tempoPoints())
                at140 = at140 || std::abs(point.beatsPerMinute - 140.0) < 1e-6;
            check(at140, "le tempo est à 140");

            const domain::Track* omnisphere = nullptr;
            for (const auto& track : state_.tracks())
            {
                for (const auto& plugin : track.plugins)
                {
                    if (juce::String{plugin.ref.name}.containsIgnoreCase("omnisphere"))
                        omnisphere = &track;
                }
            }
            check(omnisphere != nullptr, "une piste porte Omnisphere");

            std::vector<domain::Note> notes;
            if (omnisphere != nullptr)
            {
                for (const auto& pattern : state_.patterns())
                {
                    if (const auto* row = pattern.findClipForTrack(omnisphere->id); row != nullptr)
                        notes.insert(notes.end(), row->notes.begin(), row->notes.end());
                }
            }
            check(!notes.empty(), std::to_string(notes.size()) + " notes sur la piste Omnisphere");

            // The key is the one the answer names. Read from the notes it could
            // not be: the triads of A minor are the notes of C major, and only
            // the tonic tells them apart. What is checked is that what the
            // copilot says it did is what it did.
            const auto said = juce::String::fromUTF8(last != nullptr ? last->text.c_str() : "").toLowerCase();
            std::optional<domain::generation::Key> tonality;
            for (int tonic = 0; tonic < 12 && !tonality.has_value(); ++tonic)
            {
                const domain::generation::Key minor{tonic, domain::generation::Mode::minor};
                if (said.contains(
                        juce::String::fromUTF8(domain::generation::describe(minor).c_str()).toLowerCase()))
                    tonality = minor;
            }
            check(
                tonality.has_value(),
                "la réponse annonce une tonalité mineure : " +
                    (tonality.has_value() ? domain::generation::describe(*tonality) : std::string{"aucune"}));
            auto inKey = tonality.has_value();
            for (const auto& written : notes)
                inKey = inKey && domain::generation::inScale(written.pitch, *tonality);
            check(inKey, "toutes les notes sont dans cette tonalité");

            std::map<long long, std::vector<int>> onsets;
            for (const auto& written : notes)
                onsets[std::llround(written.startBeats * 960.0)].push_back(written.pitch);
            auto triads = tonality.has_value() && !onsets.empty();
            for (const auto& [at, pitches] : onsets)
            {
                std::vector<domain::generation::WeightedPitch> chord;
                for (const auto pitch : pitches)
                    chord.push_back({pitch, 1.0});
                const auto found =
                    tonality.has_value() ? domain::generation::detectChord(chord, *tonality) : std::nullopt;
                auto inside = found.has_value() && pitches.size() == 3;
                for (const auto pitch : pitches)
                    inside = inside && found.has_value() &&
                             domain::generation::isChordTone(pitch, *found, *tonality);
                triads = triads && inside;
            }
            check(triads, std::to_string(onsets.size()) + " attaques, chacune une triade de la tonalité");

            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == savedState_,
                  "un Ctrl+Z : le projet d'avant, à l'octet près");
        });
}

} // namespace daw::app
