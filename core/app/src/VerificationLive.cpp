#include "LivePlay.h"
#include "Verification.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/live/TypingKeyboard.h"
#include "daw/domain/serialization/Json.h"
#include "daw/engine/LiveInput.h"
#include "daw/ui/panels/TransportPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

// --verify-jeu (S23): playing live and recording, without a keyboard plugged
// in. The keys are pressed the way the keyboard thread presses them — a scan
// code into the TypingKeyboard, a message into the router as a MIDI input
// would —, at instants of the input clock; what they play is measured where
// it is heard, on the meters of the tracks; a take is read in the project.
//
// What it does not prove: Raw Input itself (Windows hands the scan codes),
// a MIDI keyboard really unplugged (the router's release is what an unplug
// runs), the sound reaching the ear on the beat (the card's latency is the
// one it declares; a loopback cable proves it, in the phase of trials).

namespace daw::app
{
namespace
{

constexpr double heardDb = -60.0;
constexpr double silentDb = -90.0;
constexpr int sim = domain::live::Router::simulation;
constexpr int scanW = 0x2C; // the C of the bottom row

std::string fixed(double value, int decimals = 2)
{
    return juce::String(value, decimals).toStdString();
}

} // namespace

struct Verification::PlayRun
{
    domain::TrackId lead;
    domain::TrackId bass;
    domain::PatternId pattern;
    std::string before;
    std::size_t depth{0};
    std::string beforeTake;
    std::size_t depthBeforeTake{0};
    double latency{0.0};
    std::vector<double> wanted; // the beats the take's notes were played on, as heard
    double lastAt{0.0};
    juce::TextEditor* field{nullptr};
    std::size_t keyIndex{0};
    bool keyDown{false};
    double keyAt{0.0};
    std::string wrongKeys;
};

engine::LiveInputPlugin* Verification::liveInputOf(const domain::TrackId& track) const
{
    for (auto* audio : tracktion::getAudioTracks(edit_))
    {
        for (auto* input : audio->pluginList.getPluginsOfType<engine::LiveInputPlugin>())
        {
            if (input != nullptr && input->track() == juce::String(track.toString()))
                return input;
        }
    }
    return nullptr;
}

void Verification::buildPlay()
{
    auto run = std::make_shared<PlayRun>();
    if (live_ == nullptr || router_ == nullptr)
    {
        add("le jeu en direct est là", [this] { check(false, "le jeu en direct est branché"); });
        return;
    }

    add(
        "deux pistes, un pattern de quatre mesures en PAT, la première choisie dans le rack",
        [this, run]
        {
            run->lead = domain::TrackId::generate();
            run->bass = domain::TrackId::generate();
            run->pattern = domain::PatternId::generate();
            check(bus_.execute(std::make_unique<domain::AddTrack>(run->lead, "Lead")).ok(), "la piste Lead");
            check(bus_.execute(std::make_unique<domain::AddTrack>(run->bass, "Basse")).ok(),
                  "la piste Basse");
            check(
                bus_.execute(std::make_unique<domain::CreatePattern>(run->pattern, std::string{}, 16.0)).ok(),
                "le pattern");
            check(bus_.execute(
                          std::make_unique<domain::TransportSetMode>(domain::PlayMode::pattern, run->pattern))
                      .ok(),
                  "PAT sur le pattern");
            selection_.selectTrack(run->lead);
            selection_.selectPattern(run->pattern);
            live_->follow();
        },
        [this, run] { return liveInputOf(run->lead) != nullptr && live_->target() == run->lead.toString(); },
        4000.0);

    add("la piste jouée est celle du canal choisi, et chaque piste a son entrée du jeu",
        [this, run]
        {
            check(live_->target() == run->lead.toString(), "les touches jouent « Lead »");
            check(liveInputOf(run->lead) != nullptr && liveInputOf(run->lead)->bound(),
                  "Lead a son entrée, liée");
            check(liveInputOf(run->bass) != nullptr && liveInputOf(run->bass)->bound(),
                  "Basse a son entrée, liée");
        });

    // --- playing, nothing written
    add(
        "jouer un la sans enregistrer : il s'entend sur Lead, et seulement là",
        [this, run]
        {
            run->before = domain::json::write(state_.toValue());
            run->depth = depth();
            check(router_->noteOn(sim, 1, 69, 100, domain::live::now()), "la note part");
        },
        [this, run] { return levelOf(run->lead.toString()).peakDb > heardDb; },
        3000.0);

    add("la latence, de la touche au premier échantillon rendu",
        [this, run]
        {
            check(levelOf(run->bass.toString()).peakDb < silentDb, "Basse reste muette");
            const auto* input = liveInputOf(run->lead);
            if (input == nullptr)
                return;
            const auto placed = input->lastPlaced();
            const auto toInstrument = placed.rendered - placed.played;
            const auto card = edit_.engine.getDeviceManager().getOutputLatencySeconds();
            const auto& devices = edit_.engine.getDeviceManager();
            note("carte : « " + (output_ != nullptr ? output_->outputName().toStdString() : std::string{}) +
                 " », " + fixed(devices.getSampleRate(), 0) + " Hz, tampon de " +
                 std::to_string(devices.getBlockSize()) + " échantillons (" +
                 fixed(devices.getBlockSizeMs(), 2) + " ms)");
            note("de la touche au premier échantillon rendu : " + fixed(toInstrument * 1000.0) +
                 " ms (l'attente régulière : " + fixed(input->wait() * 1000.0) + " ms)");
            note("latence de sortie déclarée par la carte : " + fixed(card * 1000.0) +
                 " ms ; de la touche à l'oreille : " + fixed((toInstrument + card) * 1000.0) + " ms");
            check(placed.played > 0.0 && toInstrument >= 0.0, "la note a été posée après avoir été jouée");
            check(std::abs(toInstrument - input->wait()) < 0.001,
                  "elle attend l'attente régulière, à la milliseconde : " + fixed(toInstrument * 1000.0) +
                      " ms");
        });

    add(
        "relâchée, la note se tait",
        [this] { check(router_->noteOff(sim, 1, 69, domain::live::now()), "le relâché part"); },
        [this, run] { return levelOf(run->lead.toString()).peakDb < silentDb; },
        3000.0);

    add("jouer n'écrit rien : le projet et son historique sont ceux d'avant, à l'octet",
        [this, run]
        {
            check(domain::json::write(state_.toValue()) == run->before, "le projet, à l'octet");
            check(depth() == run->depth, "l'historique : " + std::to_string(depth()) + " entrées");
        });

    // --- the computer's keyboard
    add(
        "le clavier de l'ordinateur : W joue le do de la rangée du bas",
        [this]
        {
            if (!live_->keyboardAvailable())
                note("Raw Input absent sur cette machine : le mode est allumé par la vérification, "
                     "les touches sont pressées par leur scan code");
            key(juce::KeyPress{'t', juce::ModifierKeys::ctrlModifier, 0});
            if (!live_->keyboardPlaying())
                live_->keys().setPlaying(true);
            live_->keys().setForeground(true);
            check(live_->keys().key(scanW, false, true, domain::live::now()), "W est une note");
        },
        [this, run] { return levelOf(run->lead.toString()).peakDb > heardDb; },
        3000.0);

    // A key at a time: what reaches the instrument is the note of its place.
    // (Not all 37 at once: no keyboard of a computer holds that many, and 4OSC
    // itself leaves a voice behind past its 32 — see the debts of S23.)
    add(
        "chaque touche des deux rangées joue sa hauteur, une à une",
        [this, run]
        {
            static_cast<void>(live_->keys().key(scanW, false, false, domain::live::now()));
            run->keyIndex = 0;
            run->wrongKeys.clear();
        },
        [this, run]
        {
            const auto& table = domain::live::TypingKeyboard::table();
            const auto* input = liveInputOf(run->lead);
            if (input == nullptr || run->keyIndex >= table.size())
                return true;
            const auto& entry = table[run->keyIndex];
            const auto wanted = 60 + entry.semitone;
            if (!run->keyDown)
            {
                static_cast<void>(live_->keys().key(entry.scanCode, false, true, domain::live::now()));
                run->keyDown = true;
                run->keyAt = domain::live::now();
                return false;
            }
            if (input->lastPlaced().note != wanted && domain::live::now() - run->keyAt < 0.5)
                return false; // not placed yet
            if (input->lastPlaced().note != wanted)
                run->wrongKeys += std::string(entry.azerty) + " ";
            static_cast<void>(live_->keys().key(entry.scanCode, false, false, domain::live::now()));
            run->keyDown = false;
            ++run->keyIndex;
            return false;
        },
        30000.0);

    add("les 37 touches : chacune sa note, par sa place",
        [this, run]
        {
            check(run->keyIndex == domain::live::TypingKeyboard::table().size(),
                  std::to_string(run->keyIndex) + " touches jouées sur " +
                      std::to_string(domain::live::TypingKeyboard::table().size()));
            check(run->wrongKeys.empty(),
                  "aucune touche fausse" + (run->wrongKeys.empty() ? std::string{} : " : " + run->wrongKeys));
        });

    add(
        "un accord de quatre touches (W, C, B, ;) tenu puis relâché : quatre notes, puis le silence",
        [this]
        {
            for (const auto scan : {0x2C, 0x2E, 0x30, 0x33})
                static_cast<void>(live_->keys().key(scan, false, true, domain::live::now()));
            check(router_->held() == 4, std::to_string(router_->held()) + " notes tenues");
            for (const auto scan : {0x2C, 0x2E, 0x30, 0x33})
                static_cast<void>(live_->keys().key(scan, false, false, domain::live::now()));
            check(router_->held() == 0, "plus rien de tenu");
        },
        [this, run] { return levelOf(run->lead.toString()).peakDb < silentDb; },
        3000.0);

    add("F et Ctrl+C ne jouent pas : F cadre, Ctrl+C copie",
        [this]
        {
            check(!live_->keys().key(0x21, false, true, domain::live::now()), "F n'est pas une note");
            static_cast<void>(live_->keys().key(0x21, false, false, domain::live::now()));
            static_cast<void>(live_->keys().key(0x1D, false, true, domain::live::now()));
            check(!live_->keys().key(0x2E, false, true, domain::live::now()), "Ctrl+C n'est pas une note");
            static_cast<void>(live_->keys().key(0x2E, false, false, domain::live::now()));
            static_cast<void>(live_->keys().key(0x1D, false, false, domain::live::now()));
            check(router_->held() == 0, "rien de tenu");
        });

    add(
        "dans un champ de texte, on tape du texte : le focus dans le champ du copilote",
        [this, run]
        {
            run->field = nullptr;
            if (auto* copilot = panel("copilot"); copilot != nullptr)
            {
                for (auto* child : copilot->getChildren())
                    if (auto* editor = dynamic_cast<juce::TextEditor*>(child);
                        editor != nullptr && run->field == nullptr)
                        run->field = editor;
            }
            check(run->field != nullptr, "le champ du copilote");
            if (run->field != nullptr)
            {
                run->field->setText({}, false);
                run->field->grabKeyboardFocus();
            }
        },
        // The focus listener is told after the message loop has turned.
        [this, run]
        { return run->field == nullptr || !run->field->hasKeyboardFocus(false) || live_->keys().typing(); },
        2000.0);

    add("le clavier sait qu'on tape : W ne joue pas, le champ reçoit « w »",
        [this, run]
        {
            if (run->field == nullptr)
                return;
            if (run->field->hasKeyboardFocus(false))
            {
                check(live_->keys().typing(), "le clavier sait qu'un champ de texte a le focus");
            }
            else
            {
                // Under a virtual screen the window may not take the focus at
                // all: what the focus listener would do is done by hand, and
                // said.
                note("le champ n'a pas reçu le focus (fenêtre sans focus) : l'état « on tape » est posé à la "
                     "main");
                live_->keys().setTyping(true);
            }
            check(!live_->keys().key(scanW, false, true, domain::live::now()), "W ne joue pas");
            static_cast<void>(live_->keys().key(scanW, false, false, domain::live::now()));
            run->field->insertTextAtCaret("w");
            check(run->field->getText() == "w", "le champ a reçu « w »");
            run->field->setText({}, false);
            run->field->giveAwayKeyboardFocus();
            live_->keys().setTyping(false);
        });

    // --- the stuck notes, each case ending in silence
    const auto stuck = [this, run](const std::string& title, std::function<void()> holdThenBreak)
    {
        add(
            "note tenue, " + title,
            [holdThenBreak] { holdThenBreak(); },
            [this, run]
            {
                return levelOf(run->lead.toString()).peakDb < silentDb &&
                       levelOf(run->bass.toString()).peakDb < silentDb && router_->held() == 0;
            },
            4000.0);
    };

    stuck("la piste changée avant le relâché : Lead se tait, Basse n'a rien joué",
          [this, run]
          {
              selection_.selectTrack(run->lead);
              live_->follow();
              static_cast<void>(router_->noteOn(sim, 1, 64, 100, domain::live::now()));
              selection_.selectTrack(run->bass);
              live_->follow();
              static_cast<void>(router_->noteOff(sim, 1, 64, domain::live::now()));
              selection_.selectTrack(run->lead);
              live_->follow();
          });
    stuck("la fenêtre passe derrière : le clavier de l'ordinateur relâche",
          [this]
          {
              static_cast<void>(live_->keys().key(scanW, false, true, domain::live::now()));
              live_->keys().setForeground(false);
              live_->keys().setForeground(true);
          });
    stuck("un clavier MIDI débranché, pédale enfoncée : ce qu'il tenait est relâché",
          [this]
          {
              const auto source = domain::live::Router::firstMidiInput;
              static_cast<void>(router_->controller(source, 1, 64, 127, domain::live::now()));
              static_cast<void>(router_->noteOn(source, 1, 60, 100, domain::live::now()));
              static_cast<void>(router_->noteOff(source, 1, 60, domain::live::now()));
              // What MidiKeyboards does when Tracktion no longer lists it.
              static_cast<void>(router_->release(source, domain::live::now()));
          });
    stuck("la carte son perdue puis rouverte",
          [this]
          {
              static_cast<void>(router_->noteOn(sim, 1, 67, 100, domain::live::now()));
              if (output_ != nullptr)
                  output_->loseOutputForTest();
              else
                  router_->silence(domain::live::now());
          });
    add(
        "une carte est rouverte",
        {},
        [this] { return output_ == nullptr || output_->outputName().isNotEmpty(); },
        4000.0);
    add(
        "note tenue pendant que le morceau part et s'arrête : elle sonne toujours, puis se tait au relâché",
        [this] { static_cast<void>(router_->noteOn(sim, 1, 72, 100, domain::live::now())); },
        [this, run] { return levelOf(run->lead.toString()).peakDb > heardDb; },
        3000.0);
    add(
        "Espace, puis Espace",
        [this] { key(juce::KeyPress{juce::KeyPress::spaceKey}); },
        [this] { return clock_.isPlaying(); },
        3000.0);
    add(
        "arrêté, la note tenue sonne encore",
        [this] { key(juce::KeyPress{juce::KeyPress::spaceKey}); },
        [this, run] { return !clock_.isPlaying() && levelOf(run->lead.toString()).peakDb > heardDb; },
        3000.0);
    add(
        "relâchée, le silence",
        [this] { static_cast<void>(router_->noteOff(sim, 1, 72, domain::live::now())); },
        [this, run] { return levelOf(run->lead.toString()).peakDb < silentDb; },
        3000.0);

    // --- the take
    add(
        "Ctrl+R en PAT : une mesure de décompte, au clic",
        [this, run]
        {
            run->beforeTake = domain::json::write(state_.toValue());
            run->depthBeforeTake = depth();
            live_->setCountIn(true);
            key(juce::KeyPress{'r', juce::ModifierKeys::ctrlModifier, 0});
            check(live_->recording() == ui::LiveHost::Recording::counting, "le décompte");
            check(edit_.clickTrackEnabled.get(), "le clic est là");
        },
        [this] { return live_->recording() == ui::LiveHost::Recording::recording; },
        8000.0);

    add(
        "quatre notes jouées sur les temps 1 à 4 de la passe suivante, une de plus sur le 3 et demi",
        [this, run]
        {
            run->latency = edit_.engine.getDeviceManager().getOutputLatencySeconds();
            domain::live::Position position;
            if (!router_->position(position))
            {
                check(false, "la position du morceau est publiée");
                return;
            }
            const auto toSeconds = [this](double beats)
            { return edit_.tempoSequence.toTime(tracktion::BeatPosition::fromBeats(beats)).inSeconds(); };
            const auto loop = toSeconds(16.0);
            // The next pass: from the loop's start after now.
            auto passStart = position.editSeconds;
            passStart = loop - std::fmod(passStart, loop);
            const auto at = [&](double beats)
            { return position.clock + passStart + toSeconds(beats) + run->latency; };
            run->wanted = {0.0, 1.0, 2.0, 3.0, 3.5};
            const int pitches[] = {60, 62, 64, 65, 67};
            for (std::size_t index = 0; index < run->wanted.size(); ++index)
            {
                const auto start = at(run->wanted[index]);
                static_cast<void>(router_->noteOn(sim, 1, pitches[index], 90, start));
                static_cast<void>(router_->noteOff(sim, 1, pitches[index], start + toSeconds(0.25)));
                run->lastAt = start + toSeconds(0.25);
            }
            note("notes datées sur la passe qui commence dans " + fixed(passStart, 3) + " s, latence " +
                 fixed(run->latency * 1000.0) + " ms");
        },
        [this, run] { return live_->takeNotes().size() >= run->wanted.size(); },
        20000.0);

    add("les notes de la prise se voient arriver, sans être dans le projet",
        [this, run]
        {
            if (auto* transport = dynamic_cast<ui::TransportPanel*>(panel("transport")); transport != nullptr)
            {
                check(transport->recordButtonLit(), "● est allumé");
                note("le transport dit : « " + transport->liveLine().toStdString() + " »");
            }
            check(live_->takeNotes().size() == run->wanted.size(),
                  std::to_string(live_->takeNotes().size()) + " notes dans la prise en cours");
            check(domain::json::write(state_.toValue()) == run->beforeTake, "le projet n'a pas encore bougé");
            snapshot("jeu-prise-en-cours");
        });

    add("les notes se jouent", {}, [run] { return domain::live::now() > run->lastAt + 0.3; }, 20000.0);

    add(
        "Ctrl+R : la prise est écrite, en un groupe",
        [this] { key(juce::KeyPress{'r', juce::ModifierKeys::ctrlModifier, 0}); },
        [this] { return live_->recording() == ui::LiveHost::Recording::idle; },
        4000.0);

    add("les notes écrites sont celles jouées, sur le temps, à 2 ms près",
        [this, run]
        {
            note(live_->recordingSaid());
            const auto* pattern = state_.findPattern(run->pattern);
            const auto* row = pattern != nullptr ? pattern->findClipForTrack(run->lead) : nullptr;
            check(row != nullptr, "la rangée de Lead est ouverte dans le pattern");
            if (row == nullptr)
                return;
            check(row->notes.size() == run->wanted.size(),
                  std::to_string(row->notes.size()) + " notes écrites");
            const auto beatsPerSecond = edit_.tempoSequence.getBpmAt(tracktion::TimePosition{}) / 60.0;
            const auto tolerance = 0.002 * beatsPerSecond;
            double worst = 0.0;
            for (std::size_t index = 0; index < std::min(row->notes.size(), run->wanted.size()); ++index)
            {
                const auto& written = row->notes[index];
                worst = std::max(worst, std::abs(written.startBeats - run->wanted[index]));
                check(std::abs(written.lengthBeats - 0.25) < tolerance + 0.01,
                      "longueur " + fixed(written.lengthBeats, 3) + " temps");
                check(written.velocity == 90, "vélocité " + std::to_string(written.velocity));
            }
            note("écart le plus grand au temps : " + fixed(worst / beatsPerSecond * 1000.0, 3) + " ms");
            check(worst <= tolerance, "chaque note sur son temps, à 2 ms près");
            check(depth() == run->depthBeforeTake + 1, "une seule entrée d'historique");
        });

    add("un Ctrl+Z retire la prise, à l'octet",
        [this, run]
        {
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == run->beforeTake,
                  "le projet d'avant la prise, à l'octet");
        });
}

} // namespace daw::app
