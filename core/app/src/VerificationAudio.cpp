#include "LivePlay.h"
#include "Verification.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/engine/AudioSettings.h"
#include "daw/engine/LiveInput.h"
#include "daw/ui/panels/AudioPanel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

// --verify-audio (S24): the « Audio » window, on this machine's card.
//
// What it proves:
//   - a buffer chosen in the window is the one the card opens, read back
//     from the engine and measured on the blocks themselves;
//   - the latency the window shows is the one the live input measures, the
//     figure --verify-jeu gives;
//   - a buffer changed while the song plays and a note is held leaves no
//     note sounding, and the card plays on;
//   - a setup that does not open gives the old one back, and says so;
//   - the person's audio settings are the same, to the byte, after the run:
//     the engine played with a copy of them in the run's folder.
// What it does not prove: the ear. The card's own latency is the one it
// declares; a loopback cable measures it, in the phase of trials.

namespace daw::app
{
namespace
{

constexpr double heardDb = -60.0;
constexpr double silentDb = -90.0;
constexpr int sim = domain::live::Router::simulation;

// Blocks the clock must have measured on a card before its numbers count.
constexpr std::int64_t blocksToMeasure = 40;

// A card just opened settles for a second; its blocks are then measured for
// three: the opening's own hiccups are not the card's regime.
constexpr double settleMs = 1000.0;
constexpr double measureMs = 3000.0;

std::string fixed(double value, int decimals = 2)
{
    return juce::String(value, decimals).toStdString();
}

juce::File personalSettings()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("DAW IA")
        .getChildFile("Settings.xml");
}

// The file's bytes, compared whole: « the same, to the byte ».
juce::MemoryBlock bytesOf(const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (file.existsAsFile())
        file.loadFileAsData(bytes);
    return bytes;
}

std::string fingerprint(const juce::MemoryBlock& bytes)
{
    return std::to_string(bytes.getSize()) + " octets, " +
           juce::String::toHexString(
               static_cast<juce::int64>(juce::String::fromUTF8(static_cast<const char*>(bytes.getData()),
                                                               static_cast<int>(bytes.getSize()))
                                            .hashCode64()))
               .toStdString();
}

std::string describe(const engine::AudioSettings::Choice& choice)
{
    return "« " + choice.output.toStdString() + " », " + choice.type.toStdString() + ", " +
           std::to_string(choice.buffer) + " échantillons";
}

} // namespace

struct Verification::AudioRun
{
    juce::MemoryBlock settingsBefore;
    juce::Time startedAt;
    engine::AudioSettings::Choice first;
    engine::AudioSettings::Choice chosen;
    domain::TrackId lead;
    domain::PatternId pattern;
    double settleUntilMs{0.0};
    double measuredFromMs{0.0};
    double restartWaitFrom{0.0};
    std::vector<engine::AudioSettings::Choice> regimes;
};

void Verification::buildAudio()
{
    auto run = std::make_shared<AudioRun>();
    if (audio_ == nullptr || router_ == nullptr || live_ == nullptr)
    {
        add("la carte son est là", [this] { check(false, "les réglages audio sont branchés"); });
        return;
    }

    add("tes réglages audio, avant",
        [this, run]
        {
            run->settingsBefore = bytesOf(personalSettings());
            run->startedAt = juce::Time::getCurrentTime();
            run->first = audio_->current();
            // The copy, proved by its bytes: plugins.xml, which nothing here
            // rewrites, is in the verification's folder as it is on the machine.
            const auto plugins = personalSettings().getSiblingFile("plugins.xml");
            if (plugins.existsAsFile())
                check(bytesOf(folder_.getChildFile("reglages-machine").getChildFile("plugins.xml")) ==
                          bytesOf(plugins),
                      "plugins.xml copié à l'octet dans le dossier de la vérification");
            else
                note("pas de plugins.xml sur la machine : la copie n'est prouvée qu'à la fin");
            note("réglages de la machine : " + personalSettings().getFullPathName().toStdString() + ", " +
                 fingerprint(run->settingsBefore));
            note("la carte au départ : " + describe(run->first) + ", " + fixed(audio_->sampleRate(), 0) +
                 " Hz");
            for (const auto& type : audio_->types())
            {
                std::string sizes;
                for (const auto size : audio_->buffers(type, run->first.output))
                    sizes += (sizes.empty() ? "" : ", ") + std::to_string(size);
                note(type.toStdString() + " : " + (sizes.empty() ? "aucun tampon" : sizes));
            }
        });

    add(
        "F12 ouvre la fenêtre Audio, qui montre la carte telle que le moteur l'a ouverte",
        [this] { key(juce::KeyPress{juce::KeyPress::F12Key}); },
        [this] { return panel("audio") != nullptr; },
        3000.0);

    add("ce qu'elle montre et ce qu'elle conseille",
        [this]
        {
            const auto* window = dynamic_cast<ui::AudioPanel*>(panel("audio"));
            if (window == nullptr)
            {
                check(false, "la fenêtre Audio est un AudioPanel");
                return;
            }
            const auto now = audio_->current();
            check(window->bufferShown().startsWith(juce::String(now.buffer) + " "),
                  "le tampon affiché : « " + window->bufferShown().toStdString() + " »");
            note(window->adviceLine().toStdString());
            note(window->latencyLine().toStdString());
            note(window->blocksLine().toStdString());
        });

    // --- the advice is measured: the card tried, setup by setup
    add(
        "« Tester ma carte » essaie chaque réglage, puis rouvre celui du départ",
        [this]
        {
            auto* window = dynamic_cast<ui::AudioPanel*>(panel("audio"));
            if (window == nullptr)
                return;
            window->startTrial();
            check(audio_->trialRunning(), "l'essai a commencé");
        },
        [this] { return !audio_->trialRunning(); },
        90000.0);

    add("ce que l'essai a mesuré, et le conseil qu'il en tire",
        [this, run]
        {
            const auto trials = audio_->trials();
            check(!trials.empty(), std::to_string(trials.size()) + " réglages essayés");
            for (const auto& trial : trials)
                note(trial.type + ", " + std::to_string(trial.buffer) +
                     " échantillons : " + std::to_string(trial.timing.blocks) + " blocs, un toutes les " +
                     fixed(trial.timing.meanSeconds * 1000.0) + " ms, au pire " +
                     fixed(trial.timing.worstSeconds * 1000.0) + " ms, " + std::to_string(trial.timing.late) +
                     " décrochages ; la carte déclare " + fixed(trial.outputSeconds * 1000.0) +
                     " ms ; au pire de la touche à l'oreille " +
                     fixed(domain::live::worstKeyToEar(trial) * 1000.0) + " ms" +
                     (domain::live::held(trial) ? "" : " — ne tient pas"));

            const auto advice = audio_->advice();
            note("conseil : " + advice.sentence);
            check(advice.measured, "le conseil vient de l'essai");
            // Never a setup that dropped a block; the least wait among those
            // that held, at 256 or less when one held there.
            const domain::live::CardTrial* chosen = nullptr;
            double least = 1.0e9;
            double leastSmall = 1.0e9;
            for (const auto& trial : trials)
            {
                if (trial.type == advice.type && trial.buffer == advice.buffer)
                    chosen = &trial;
                if (!domain::live::held(trial))
                    continue;
                least = std::min(least, domain::live::worstKeyToEar(trial));
                if (trial.buffer <= domain::live::advisedMaxBuffer)
                    leastSmall = std::min(leastSmall, domain::live::worstKeyToEar(trial));
            }
            const auto anyHeld = least < 1.0e9;
            if (anyHeld)
            {
                // Read from the trial's own numbers, not by held(), which
                // the advice uses: a held() gone wrong would agree with itself.
                check(chosen != nullptr && chosen->timing.late == 0 &&
                          chosen->timing.blocks >= blocksToMeasure,
                      "le réglage conseillé a tenu sans décrocher" +
                          (chosen != nullptr
                               ? " : " + std::to_string(chosen->timing.late) + " décrochages en " +
                                     std::to_string(chosen->timing.blocks) + " blocs"
                               : std::string{}));
                const auto wanted = leastSmall < 1.0e9 ? leastSmall : least;
                check(chosen != nullptr && std::abs(domain::live::worstKeyToEar(*chosen) - wanted) < 1.0e-9,
                      "c'est celui qui attend le moins, au pire, parmi ceux qui tiennent");
            }
            else
            {
                check(advice.already, "aucun réglage n'a tenu : rien n'est conseillé");
            }
            check(audio_->current() == run->first,
                  "la carte du départ est rouverte : " + describe(audio_->current()));
            check(folder_.getChildFile("reglages-machine").getChildFile("carte-audio.json").existsAsFile(),
                  "l'essai est gardé sur la machine (ici, la copie de la vérification)");
            audio_->resetTiming();
        });

    // --- a buffer chosen is the one the card opens
    add(
        "un tampon choisi dans la fenêtre est celui que la carte ouvre",
        [this, run]
        {
            auto* window = dynamic_cast<ui::AudioPanel*>(panel("audio"));
            if (window == nullptr)
                return;
            // Another buffer than the first, 256 samples when a driver offers
            // it: what the card opens is then told apart from what it had.
            // Chosen through the window, the driver first, then the buffer.
            for (const auto& type : audio_->types())
            {
                const auto sizes = audio_->buffers(type, run->first.output);
                auto size = std::find(sizes.begin(), sizes.end(), 256) != sizes.end() ? 256 : 0;
                for (const auto offered : sizes)
                    size = size != 0 ? size : (offered != run->first.buffer ? offered : 0);
                if (size == 0)
                    continue;
                run->chosen = {type, run->first.output, size};
                if (type != run->first.type)
                    window->chooseType(type.toStdString());
                window->chooseBuffer(size);
                return;
            }
            note("cette carte n'offre ni autre tampon ni autre pilote : rien à changer");
        },
        [this, run]
        {
            const auto timing = audio_->timing();
            return run->chosen.buffer == 0 ||
                   (audio_->current().buffer == run->chosen.buffer && timing.blocks >= blocksToMeasure);
        },
        8000.0);

    add(
        "une seconde pour se poser, puis trois secondes mesurées",
        [run]
        {
            run->settleUntilMs = juce::Time::getMillisecondCounterHiRes() + settleMs;
            run->measuredFromMs = 0.0;
        },
        [this, run]
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (now < run->settleUntilMs)
                return false;
            if (run->measuredFromMs == 0.0)
            {
                audio_->resetTiming();
                run->measuredFromMs = now;
                return false;
            }
            return now - run->measuredFromMs >= measureMs && audio_->timing().blocks >= blocksToMeasure;
        },
        settleMs + measureMs + 3000.0);

    add("relu au moteur et mesuré sur les blocs",
        [this, run]
        {
            if (run->chosen.buffer == 0)
            {
                check(false, "un autre tampon a pu être choisi");
                return;
            }
            const auto now = audio_->current();
            const auto timing = audio_->timing();
            note("ouvert : " + describe(now) + " ; " + audio_->said().toStdString());
            check(now.type == run->chosen.type, "le pilote : " + now.type.toStdString());
            check(now.buffer == run->chosen.buffer,
                  "le moteur relit " + std::to_string(now.buffer) + " échantillons");
            check(timing.lastSize == run->chosen.buffer,
                  "les blocs que la carte demande en font " + std::to_string(timing.lastSize));
            const auto block = run->chosen.buffer / audio_->sampleRate();
            check(std::abs(timing.meanSeconds - block) < 0.25 * block,
                  "un bloc toutes les " + fixed(timing.meanSeconds * 1000.0) + " ms (attendu " +
                      fixed(block * 1000.0) + " ms), au pire " + fixed(timing.worstSeconds * 1000.0) +
                      " ms, " + std::to_string(timing.late) + " décrochages sur " +
                      std::to_string(timing.blocks) + " blocs");
            check(audio_->said().contains("ouvert"), "la fenêtre dit ce qui s'est ouvert");
        });

    // --- the latency shown is the one the live input measures
    add(
        "une piste jouée, un pattern en PAT",
        [this, run]
        {
            run->lead = domain::TrackId::generate();
            run->pattern = domain::PatternId::generate();
            check(bus_.execute(std::make_unique<domain::AddTrack>(run->lead, "Lead")).ok(), "la piste Lead");
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

    add(
        "un la joué s'entend",
        [this] { check(router_->noteOn(sim, 1, 69, 100, domain::live::now()), "la note part"); },
        [this, run] { return levelOf(run->lead.toString()).peakDb > heardDb; },
        3000.0);

    add("la latence affichée est celle que le jeu mesure",
        [this, run]
        {
            const auto* input = liveInputOf(run->lead);
            const auto* window = dynamic_cast<ui::AudioPanel*>(panel("audio"));
            if (input == nullptr || window == nullptr)
            {
                check(false, "l'entrée du jeu et la fenêtre sont là");
                return;
            }
            const auto placed = input->lastPlaced();
            const auto measured = placed.rendered - placed.played;
            const auto card = audio_->outputLatencySeconds();
            const auto line = domain::live::describeLatency(measured, true, card);
            note("mesuré par le jeu : " + fixed(measured * 1000.0) + " ms, attente régulière " +
                 fixed(input->wait() * 1000.0) + " ms, la carte déclare " + fixed(card * 1000.0) + " ms");
            check(window->latencyLine() == juce::String::fromUTF8(line.c_str()),
                  "la fenêtre : « " + window->latencyLine().toStdString() + " »");
            // Regular only when the card's callback is: noted, the window
            // shows what was measured either way.
            note(std::abs(measured - input->wait()) < 0.001
                     ? "la note a attendu l'attente régulière, à la milliseconde"
                     : "la note a attendu " + fixed((measured - input->wait()) * 1000.0) +
                           " ms de plus que l'attente régulière : la carte n'appelle pas à intervalles "
                           "réguliers");
            check(input->wait() > run->chosen.buffer / audio_->sampleRate(),
                  "l'attente suit le tampon ouvert : plus d'un bloc de " +
                      std::to_string(run->chosen.buffer));
        });

    // --- changing the buffer while the song plays and a note is held
    add(
        "le morceau joue, le la tenu",
        [this]
        {
            if (!clock_.isPlaying())
                key(juce::KeyPress{juce::KeyPress::spaceKey});
        },
        [this, run] { return clock_.isPlaying() && levelOf(run->lead.toString()).peakDb > heardDb; },
        3000.0);

    add(
        "le tampon changé pendant la lecture : la note tenue se tait",
        [this, run]
        {
            auto* window = dynamic_cast<ui::AudioPanel*>(panel("audio"));
            if (window == nullptr)
                return;
            if (run->first.type != run->chosen.type)
                window->chooseType(run->first.type.toStdString());
            else
                window->chooseBuffer(run->first.buffer);
        },
        [this, run]
        {
            const auto* input = liveInputOf(run->lead);
            return audio_->current() == run->first && levelOf(run->lead.toString()).peakDb < silentDb &&
                   input != nullptr && input->heldNow() == 0;
        },
        6000.0);

    add(
        "le morceau n'a pas cessé : le projet joue toujours, le moteur repart",
        [this, run]
        {
            check(state_.transport().playing, "le projet joue toujours : aucun arrêt porté au domaine");
            run->restartWaitFrom = juce::Time::getMillisecondCounterHiRes();
        },
        [this] { return clock_.isPlaying(); },
        3000.0);

    add(
        "et la carte joue encore : une note neuve s'entend",
        [this, run]
        {
            note("le moteur est reparti " +
                 fixed(juce::Time::getMillisecondCounterHiRes() - run->restartWaitFrom, 0) +
                 " ms après que la note tenue s'est tue");
            check(!router_->noteOff(sim, 1, 69, domain::live::now()),
                  "le la n'était plus tenu : la réouverture l'avait relâché");
            check(router_->noteOn(sim, 1, 72, 100, domain::live::now()), "un do part");
        },
        [this, run] { return levelOf(run->lead.toString()).peakDb > heardDb; },
        3000.0);

    add(
        "relâché, le do se tait ; le morceau s'arrête",
        [this]
        {
            check(router_->noteOff(sim, 1, 72, domain::live::now()), "le relâché part");
            if (clock_.isPlaying())
                key(juce::KeyPress{juce::KeyPress::spaceKey});
        },
        [this, run] { return levelOf(run->lead.toString()).peakDb < silentDb && !clock_.isPlaying(); },
        3000.0);

    // --- a setup that does not open
    add(
        "une sortie qui n'existe pas : retour à l'ancienne, et la fenêtre le dit",
        [this, run]
        {
            const auto before = audio_->current();
            check(!audio_->apply({before.type, "Sortie qui n'existe pas", before.buffer}),
                  "le réglage est refusé");
            check(audio_->current() == before,
                  "la carte d'avant est rouverte : " + describe(audio_->current()));
            check(audio_->said().contains("ne s'ouvre pas"), "« " + audio_->said().toStdString() + " »");
            audio_->resetTiming();
        },
        [this] { return audio_->timing().blocks >= blocksToMeasure; },
        4000.0);

    add("tes réglages audio, après : les mêmes, à l'octet",
        [this, run]
        {
            const auto after = bytesOf(personalSettings());
            check(after.getSize() > 0 && after == run->settingsBefore,
                  personalSettings().getFileName().toStdString() + " : " + fingerprint(after));
            // The buffers changed above are written by the engine where it
            // keeps its settings: there, during this run.
            const auto kept = folder_.getChildFile("reglages-machine").getChildFile("Settings.xml");
            check(kept.existsAsFile() && kept.getLastModificationTime() >= run->startedAt,
                  "le moteur a écrit ses réglages dans le dossier de la vérification, pendant ce passage");
            check(!personalSettings().getSiblingFile("carte-audio.json").existsAsFile() ||
                      personalSettings().getSiblingFile("carte-audio.json").getLastModificationTime() <
                          juce::Time::getCurrentTime() - juce::RelativeTime::minutes(30),
                  "l'essai de la carte n'a rien écrit dans tes réglages");
        });
}

} // namespace daw::app
