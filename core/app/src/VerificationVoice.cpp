#include "CopilotBridge.h"
#include "NativeAudio.h"
#include "Verification.h"
#include "VoiceInput.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/live/Router.h"
#include "daw/domain/serialization/Json.h"
#include "daw/domain/voice/PushToTalk.h"
#include "daw/engine/AudioSettings.h"
#include "daw/engine/ContentStore.h"
#include "daw/ui/panels/CopilotPanel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

// --verify-voix (S25): the push-to-talk, from the key to the project, without
// a microphone and without a model. Files of the test set are played where the
// microphone would be (Microphone::injectForTest), the transcriber replays what
// Parakeet heard on them (services/tests/voix/parakeet-entendu.json), and the
// copilot answers from a table this check writes (no model, no key). With
// --voix-transcripteur parakeet the model itself transcribes; with
// --voix-micro-reel, this machine's microphone is opened too, to measure that
// the output does not change.
//
// What it proves:
//   - without the key held, not one sample is read;
//   - a sure phrase is shown, then leaves on its own, and makes the project
//     the same phrase typed makes; one Ctrl+Z takes it back, to the byte;
//   - the journal tells a phrase said from a phrase typed (the provenance's
//     context, kept in the store), and the voice is in neither;
//   - no phrase judged doubtful makes a command, on the whole test set, the
//     project identical to the byte;
//   - a phrase said that removes asks first; refused, nothing is written;
//   - every end but a release in time leaves nothing: too short, too long,
//     another key, the focus lost;
//   - the song is lowered while the key is held, and given back.

namespace daw::app
{
namespace
{

using domain::Value;
using domain::voice::PushToTalk;

constexpr int rightCtrl = PushToTalk::keyScanCode;

juce::File voiceSet()
{
    return CopilotBridge::servicesFolder().getChildFile("tests").getChildFile("voix");
}

std::vector<float> readSet(const std::string& id)
{
    std::vector<float> samples;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{
        formats.createReaderFor(voiceSet().getChildFile("synthese").getChildFile(id + ".wav"))};
    if (reader == nullptr)
        return samples;
    juce::AudioBuffer<float> buffer{1, static_cast<int>(reader->lengthInSamples)};
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, false);
    samples.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples());
    return samples;
}

std::vector<std::string> setIds()
{
    std::vector<std::string> ids;
    juce::StringArray lines;
    voiceSet().getChildFile("phrases.tsv").readLines(lines);
    for (const auto& line : lines)
        if (line.isNotEmpty() && !line.startsWith("#"))
            ids.push_back(line.upToFirstOccurrenceOf("\t", false, false).toStdString());
    return ids;
}

} // namespace

struct Verification::VoiceRun
{
    std::vector<std::string> ids; // the test set, in order
    std::size_t at{0};            // the phrase being said
    double releaseAt{0.0};        // when the key goes up, on domain::live's clock
    std::string before;           // the project before the phrase
    std::string afterSaid;        // after the sure phrase said
    std::size_t transcript{0};
    std::size_t depth{0};
    std::map<std::string, std::string> heardText; // the dry runs
    std::vector<std::string> doubtful;
    std::vector<std::string> sure;
    std::vector<double> latencies;
    std::int64_t samplesBefore{0};
    domain::TrackId snare;
    domain::TrackId guitar;
    float duckedDb{0.0f};
    juce::String output;
    juce::String formatBefore;
};

void Verification::buildVoice()
{
    auto run = std::make_shared<VoiceRun>();
    if (voice_ == nullptr || store_ == nullptr)
    {
        add("la voix est là", [this] { check(false, "la voix et le magasin sont branchés"); });
        return;
    }
    run->ids = setIds();

    const auto stateBytes = [this] { return domain::json::write(state_.toValue()); };
    const auto field = [this]() -> juce::TextEditor*
    {
        auto* copilot = dynamic_cast<ui::CopilotPanel*>(panel("copilot"));
        return copilot != nullptr ? &copilot->field() : nullptr;
    };
    const auto table = [this] { return folder_.getChildFile("copilote-table.json"); };

    // Says the phrase `id`: the file played where the microphone is, the key
    // held as long as it lasts, then released. A step for the press, one for
    // the release, one for what comes back.
    const auto say = [this, run](const std::string& title,
                                 std::function<std::string()> which,
                                 std::function<void()> whileHeld = {},
                                 bool button = false,
                                 bool hold = false)
    {
        add(
            title + " : la touche tenue",
            [this, run, which, button]
            {
                const auto id = which();
                auto samples = readSet(id);
                const auto seconds = static_cast<double>(samples.size()) / Microphone::rate16k;
                voice_->microphoneDevice().injectForTest(std::move(samples));
                run->releaseAt = domain::live::now() + seconds + 0.3;
                if (button)
                    voice_->press();
                else
                    voice_->keyForTest(rightCtrl, true, true);
            },
            [run] { return domain::live::now() >= run->releaseAt; },
            20000.0);
        add(
            title + " : relâchée",
            [this, whileHeld, button]
            {
                if (whileHeld)
                    whileHeld();
                if (button)
                    voice_->release();
                else
                    voice_->keyForTest(rightCtrl, true, false);
            },
            [this, hold]
            {
                const auto stage = voice_->stage();
                // A phrase read, not sent: held the moment it is shown, as a
                // key held by the person would.
                if (hold && stage == ui::VoiceHost::Stage::sure)
                    voice_->holdPhrase();
                return stage == ui::VoiceHost::Stage::sure || stage == ui::VoiceHost::Stage::doubtful ||
                       stage == ui::VoiceHost::Stage::failed || stage == ui::VoiceHost::Stage::idle;
            },
            60000.0);
    };

    // --- the project, the copilot, the transcriber ------------------------------------
    add(
        "un projet aux noms du jeu d'essai, le copilote par une table",
        [this, run, table]
        {
            // The tracks of the test set's project (tests/voix/noms.json).
            for (const auto* name : {"808",
                                     "Kick",
                                     "Caisse claire",
                                     "Charleys",
                                     "Lead Pluck",
                                     "Pad Ambient",
                                     "Hi Hat Roll",
                                     "Basse",
                                     "Guitare",
                                     "Vocals",
                                     "Cordes",
                                     "Clap"})
            {
                const auto id = domain::TrackId::generate();
                check(bus_.execute(std::make_unique<domain::AddTrack>(id, name, 0.0)).ok(), name);
                if (std::string{name} == "Caisse claire")
                    run->snare = id;
                if (std::string{name} == "Guitare")
                    run->guitar = id;
            }
            static_cast<void>(table().replaceWithText("{}"));
            // The window in front, whatever the machine does meanwhile: a
            // person using another application during the run must not end
            // the phrases (that case has a step of its own, below).
            voice_->inFront = [] { return true; };
            note(std::string{"transcripteur : "} + (voice_->service().installed() ? "installé" : "absent"));
        },
        [this] { return copilot_.status() == ui::CopilotHost::Status::ready; },
        60000.0);

    // --- nothing read without the key -------------------------------------------------
    add(
        "sans la touche : deux secondes, aucun échantillon lu",
        [this, run]
        {
            run->samplesBefore = voice_->microphoneDevice().samplesEverRead();
            run->releaseAt = domain::live::now() + 2.0;
        },
        [run] { return domain::live::now() >= run->releaseAt; },
        5000.0);
    add("rien n'a été lu",
        [this, run]
        {
            check(!voice_->microphoneDevice().isOpen(), "le micro n'est pas ouvert");
            check(voice_->microphoneDevice().samplesEverRead() == run->samplesBefore,
                  std::to_string(voice_->microphoneDevice().samplesEverRead() - run->samplesBefore) +
                      " échantillons lus sans la touche");
        });

    // --- what the transcriber heard, said once without sending ------------------------
    for (const auto* id : {"c03", "c07"})
    {
        const std::string phrase{id};
        say("« " + phrase + " » à blanc", [phrase] { return phrase; }, {}, false, true);
        add("« " + phrase + " » entendue, gardée de côté",
            [this, run, phrase]
            {
                const auto& heard = voice_->lastHeard();
                check(heard.has_value() && !heard->doubtful,
                      "sûre : « " + (heard.has_value() ? heard->text : std::string{}) + " »");
                if (heard.has_value())
                    run->heardText[phrase] = heard->text;
                voice_->dropPhrase();
            });
    }
    add("la table du copilote : ces deux phrases, ces commandes",
        [this, run, table]
        {
            auto* object = new juce::DynamicObject();
            const auto command = [](const char* type, juce::var arguments)
            {
                auto* call = new juce::DynamicObject();
                call->setProperty("name", type);
                call->setProperty("arguments", arguments);
                juce::Array<juce::var> list;
                list.add(juce::var{call});
                return juce::var{list};
            };
            auto* volume = new juce::DynamicObject();
            volume->setProperty("trackId", juce::String{run->snare.toString()});
            volume->setProperty("volumeDb", -3.0);
            object->setProperty(juce::String::fromUTF8(run->heardText["c03"].c_str()),
                                command("track.set_volume", juce::var{volume}));
            auto* removed = new juce::DynamicObject();
            removed->setProperty("trackId", juce::String{run->guitar.toString()});
            object->setProperty(juce::String::fromUTF8(run->heardText["c07"].c_str()),
                                command("track.remove", juce::var{removed}));
            check(table().replaceWithText(juce::JSON::toString(juce::var{object})), "écrite");
        });

    // --- a sure phrase said: shown, gone on its own, the project -----------------------
    say(
        "« Baisse la caisse claire de trois décibels », dite",
        [] { return std::string{"c03"}; },
        [this, run, stateBytes]
        {
            run->before = stateBytes();
            run->depth = depth();
            run->transcript = copilot_.transcript().size();
            run->duckedDb = edit_.getMasterVolumePlugin() != nullptr
                                ? edit_.getMasterVolumePlugin()->getVolumeDb()
                                : 0.0f;
        });
    add(
        "sûre : affichée dans le champ, partie seule après 1,5 s",
        [this, run, field]
        {
            check(std::abs(run->duckedDb - VoiceInput::duckDb) < 0.01f,
                  "le morceau baissé de 20 dB pendant la touche : " + std::to_string(run->duckedDb) + " dB");
            const auto master = edit_.getMasterVolumePlugin();
            check(master != nullptr && std::abs(master->getVolumeDb()) < 0.01f, "puis rendu à 0 dB");
            check(voice_->lastHeard().has_value() && !voice_->lastHeard()->doubtful, "jugée sûre");
            auto* editor = field();
            check(editor != nullptr && editor->getText().toStdString() == run->heardText["c03"],
                  "dans le champ : « " +
                      (editor != nullptr ? editor->getText().toStdString() : std::string{}) + " »");
            run->latencies.push_back(voice_->lastLatencySeconds());
            note("du relâchement à la phrase affichée : " + std::to_string(voice_->lastLatencySeconds()) +
                 " s");
        },
        [this, run]
        {
            return copilot_.transcript().size() > run->transcript + 1 &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        30000.0);
    add("le projet : la caisse claire à -3 dB, une entrée d'historique, dite",
        [this, run, stateBytes]
        {
            const auto* snare = state_.findTrack(run->snare);
            check(snare != nullptr && std::abs(snare->volumeDb + 3.0) < 1e-9,
                  "caisse claire : " + std::to_string(snare != nullptr ? snare->volumeDb : 0.0) + " dB");
            check(depth() == run->depth + 1, "une entrée d'historique");
            run->afterSaid = stateBytes();
            const auto& entries = history_.entries();
            const auto* last = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(last != nullptr && last->context.has_value(), "le journal garde ce qui a été entendu");
            if (last != nullptr && last->context.has_value())
            {
                const auto blob = store_->get(*last->context);
                const auto text = blob ? blob.value().toString().toStdString() : std::string{};
                check(text.find("\"source\":\"voix\"") != std::string::npos, "« source » : « voix »");
                check(text.find(run->heardText["c03"]) != std::string::npos, "le texte entendu");
                check(text.find("samples") == std::string::npos && text.size() < 4096,
                      "pas de son : " + std::to_string(text.size()) + " octets");
            }
        });
    add(
        "Ctrl+Z défait la phrase dite, à l'octet",
        [this, run, stateBytes]
        {
            check(bus_.undo().ok(), "annulé");
            check(stateBytes() == run->before, "le projet d'avant, à l'octet");
            run->transcript = copilot_.transcript().size();
            copilot_.ask(run->heardText["c03"]);
        },
        [this, run]
        {
            return copilot_.transcript().size() > run->transcript + 1 &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        30000.0);
    add("la même phrase tapée : le même projet, et le journal la dit tapée",
        [this, run, stateBytes]
        {
            check(stateBytes() == run->afterSaid, "le projet de la phrase dite, à l'octet");
            const auto& entries = history_.entries();
            const auto* last = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(last != nullptr && !last->context.has_value(), "aucun contexte de voix");
            static_cast<void>(bus_.undo());
            check(stateBytes() == run->before, "annulée");
        });

    // --- a phrase said that removes: asked first ------------------------------------
    say("« Supprime la piste guitare », dite", [] { return std::string{"c07"}; });
    add(
        "elle retire une piste : on demande d'abord",
        [this, run] { run->transcript = copilot_.transcript().size(); },
        [this] { return voice_->stage() == ui::VoiceHost::Stage::confirming; },
        30000.0);
    add(
        "refusée : rien n'est écrit",
        [this, run, stateBytes]
        {
            const auto removals = voice_->removals();
            check(!removals.empty() && removals.front() == "retire une piste",
                  "« " + (removals.empty() ? std::string{} : removals.front()) + " »");
            check(stateBytes() == run->before, "rien d'écrit en attendant");
            voice_->confirm(false);
        },
        [this, run]
        {
            return copilot_.transcript().size() > run->transcript &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        30000.0);
    add("refusée : le projet à l'octet",
        [this, run, stateBytes] { check(stateBytes() == run->before, "le projet d'avant, à l'octet"); });
    say("« Supprime la piste guitare », dite encore", [] { return std::string{"c07"}; });
    add(
        "confirmée : la piste retirée",
        {},
        [this] { return voice_->stage() == ui::VoiceHost::Stage::confirming; },
        30000.0);
    add(
        "confirmée",
        [this, run]
        {
            run->transcript = copilot_.transcript().size();
            voice_->confirm(true);
        },
        [this, run]
        {
            return copilot_.transcript().size() > run->transcript &&
                   copilot_.status() != ui::CopilotHost::Status::working;
        },
        30000.0);
    add("la guitare retirée, puis rendue par un Ctrl+Z",
        [this, run, stateBytes]
        {
            check(state_.findTrack(run->guitar) == nullptr, "la piste Guitare n'est plus là");
            static_cast<void>(bus_.undo());
            check(stateBytes() == run->before, "rendue, à l'octet");
        });

    // --- every phrase of the test set: a doubtful one makes nothing -----------------
    for (std::size_t index = 0; index < run->ids.size(); ++index)
    {
        say(
            "phrase " + run->ids[index],
            [run, index] { return run->ids[index]; },
            [this, run, stateBytes]
            {
                run->before = stateBytes();
                run->transcript = copilot_.transcript().size();
            },
            index == 0, // the first by the button on the screen
            true);
        add(
            "phrase " + run->ids[index] + " : ce qu'elle fait",
            [this, run, index]
            {
                const auto& heard = voice_->lastHeard();
                const auto text = heard.has_value() ? heard->text : std::string{};
                if (heard.has_value() && !heard->doubtful)
                {
                    // Not sent: held the moment it was shown, then dropped.
                    run->sure.push_back(run->ids[index]);
                    note("sûre : « " + text + " »");
                }
                else
                {
                    run->doubtful.push_back(run->ids[index]);
                    note("douteuse : « " + text + " » — " + voice_->message());
                }
                run->latencies.push_back(voice_->lastLatencySeconds());
                run->releaseAt = domain::live::now() + ui::VoiceHost::sureDelaySeconds + 1.0;
            },
            [run] { return domain::live::now() >= run->releaseAt; },
            10000.0);
        add("phrase " + run->ids[index] + " : rien n'est parti",
            [this, run, stateBytes]
            {
                check(copilot_.transcript().size() == run->transcript, "le copilote n'a rien reçu");
                check(stateBytes() == run->before, "le projet à l'octet");
                voice_->dropPhrase();
            });
    }
    add("le jeu d'essai : douteuses et sûres",
        [this, run]
        {
            std::string doubtful;
            for (const auto& id : run->doubtful)
                doubtful += id + " ";
            note(std::to_string(run->doubtful.size()) + " douteuses (aucune n'a rien fait) : " + doubtful);
            note(std::to_string(run->sure.size()) + " sûres, retenues avant de partir");
            for (const auto* built : {"d01", "d02", "d03", "d04", "d05", "d06", "d07"})
                check(std::find(run->doubtful.begin(), run->doubtful.end(), built) != run->doubtful.end(),
                      std::string{built} + " est douteuse");
            auto times = run->latencies;
            std::sort(times.begin(), times.end());
            if (!times.empty())
                note("du relâchement à la phrase affichée : médiane " +
                     std::to_string(times[times.size() / 2]) + " s, au pire " + std::to_string(times.back()) +
                     " s, sur " + std::to_string(times.size()) + " phrases");
        });

    // --- every other end leaves nothing ------------------------------------------------
    const auto heldEnd = [this, run, stateBytes](const std::string& title,
                                                 double holdSeconds,
                                                 std::function<void()> during,
                                                 const std::string& said)
    {
        add(
            title,
            [this, run, stateBytes, holdSeconds]
            {
                run->before = stateBytes();
                run->transcript = copilot_.transcript().size();
                voice_->microphoneDevice().injectForTest(readSet("c01"));
                voice_->keyForTest(rightCtrl, true, true);
                run->releaseAt = domain::live::now() + holdSeconds;
            },
            [run] { return domain::live::now() >= run->releaseAt; },
            30000.0);
        add(
            title + " : rien",
            [this, during] { during(); },
            [this] { return voice_->stage() == ui::VoiceHost::Stage::idle; },
            5000.0);
        add(title + " : dit, et le projet à l'octet",
            [this, run, stateBytes, said]
            {
                check(voice_->message().find(said) != std::string::npos, "« " + voice_->message() + " »");
                check(!voice_->lastHeard().has_value(), "aucune phrase");
                check(copilot_.transcript().size() == run->transcript, "le copilote n'a rien reçu");
                check(stateBytes() == run->before, "le projet à l'octet");
                check(!voice_->microphoneDevice().isOpen(), "le micro fermé");
            });
    };
    // Released after 0.3 s by the clock, not by the next step: a step takes
    // longer than that to begin.
    add(
        "relâchée après 0,3 s",
        [this, run, stateBytes]
        {
            run->before = stateBytes();
            run->transcript = copilot_.transcript().size();
            voice_->microphoneDevice().injectForTest(readSet("c01"));
            voice_->keyForTest(rightCtrl, true, true);
            juce::Timer::callAfterDelay(300, [this] { voice_->keyForTest(rightCtrl, true, false); });
            run->releaseAt = domain::live::now() + 0.6;
        },
        [this, run]
        { return domain::live::now() >= run->releaseAt && voice_->stage() == ui::VoiceHost::Stage::idle; },
        5000.0);
    add("relâchée après 0,3 s : dit, et le projet à l'octet",
        [this, run, stateBytes]
        {
            check(voice_->message().find("trop court") != std::string::npos, "« " + voice_->message() + " »");
            check(!voice_->lastHeard().has_value(), "aucune phrase");
            check(copilot_.transcript().size() == run->transcript, "le copilote n'a rien reçu");
            check(stateBytes() == run->before, "le projet à l'octet");
            check(!voice_->microphoneDevice().isOpen(), "le micro fermé");
        });
    heldEnd(
        "une autre touche pendant la phrase (Z)",
        1.0,
        [this]
        {
            voice_->keyForTest(0x2C, false, true);
            voice_->keyForTest(0x2C, false, false);
            voice_->keyForTest(rightCtrl, true, false);
        },
        "autre touche");
    heldEnd(
        "la fenêtre perd la main pendant la phrase",
        1.0,
        [this] { voice_->inFront = [] { return false; }; },
        "perdu la main");
    add("la main rendue", [this] { voice_->inFront = [] { return true; }; });
    heldEnd("tenue plus de 15 s", 15.4, [] {}, "15 secondes");

    // --- this machine's microphone, when asked: the output does not change ------------
    if (!realMicrophone_)
        return;
    const auto openAndCompare = [this, run](const std::string& where)
    {
        add(
            "le vrai micro, " + where + " : la sortie avant",
            [this, run]
            {
                auto* device = edit_.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
                run->output = device != nullptr ? device->getName() : juce::String{};
                const auto endpoint = native::renderEndpoint(run->output);
                run->formatBefore = endpoint.has_value() ? endpoint->format : juce::String{};
                note("sortie : « " + run->output.toStdString() + " », " + run->formatBefore.toStdString() +
                     (endpoint.has_value() && endpoint->bluetooth ? " (Bluetooth)" : ""));
                for (const auto& input : voice_->microphones())
                    note(std::string{"micro : « "} + input.name + " »" +
                         (input.bluetooth ? " (Bluetooth)" : ""));
                note("micro choisi : « " + voice_->microphone() + " »");
                voice_->microphoneDevice().injectForTest({});
                voice_->keyForTest(rightCtrl, true, true);
                run->releaseAt = domain::live::now() + 3.0;
            },
            [run] { return domain::live::now() >= run->releaseAt; },
            10000.0);
        add(
            "le vrai micro ouvert, " + where + " : la même sortie, au même format",
            [this, run]
            {
                check(voice_->microphoneDevice().isOpen(),
                      "ouvert : « " + voice_->microphoneDevice().openedName().toStdString() + " »");
                check(voice_->microphoneDevice().samplesEverRead() > 0, "il lit");
                auto* device = edit_.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
                const auto output = device != nullptr ? device->getName() : juce::String{};
                const auto endpoint = native::renderEndpoint(output);
                const auto format = endpoint.has_value() ? endpoint->format : juce::String{};
                check(output == run->output, "la même sortie : « " + output.toStdString() + " »");
                check(format == run->formatBefore && endpoint.has_value() && endpoint->active,
                      "le même format : " + format.toStdString());
                voice_->keyForTest(0x2C, false, true); // ends it, nothing sent
                voice_->keyForTest(0x2C, false, false);
                voice_->keyForTest(rightCtrl, true, false);
            },
            [this] { return voice_->stage() == ui::VoiceHost::Stage::idle; },
            5000.0);
    };
    openAndCompare("sortie actuelle");

    // The trap named by the founder: the song on a Bluetooth headset, the
    // microphone open. When such a headset is there, the card is moved to it,
    // the microphone chosen (never the headset's) opened, the format compared.
    if (audio_ == nullptr)
        return;
    auto first = std::make_shared<engine::AudioSettings::Choice>();
    auto headset = std::make_shared<juce::String>();
    add(
        "un casque Bluetooth en sortie, s'il y en a un",
        [this, first, headset]
        {
            *first = audio_->current();
            for (const auto& output : audio_->outputs("Windows Audio"))
            {
                const auto endpoint = native::renderEndpoint(output);
                if (endpoint.has_value() && endpoint->bluetooth && endpoint->active)
                {
                    *headset = endpoint->name;
                    break;
                }
            }
            if (headset->isEmpty())
            {
                note("aucun casque Bluetooth branché : le piège n'est pas éprouvé ici");
                return;
            }
            check(audio_->apply({"Windows Audio", *headset, first->buffer}),
                  "la sortie sur « " + headset->toStdString() + " »");
            audio_->resetTiming();
        },
        [this, headset] { return headset->isEmpty() || audio_->timing().blocks >= 40; },
        10000.0);
    openAndCompare("casque Bluetooth en sortie");
    add("la carte du départ rouverte",
        [this, first, headset]
        {
            if (headset->isEmpty())
                return;
            check(audio_->apply(*first), "la carte du départ");
        });
}

} // namespace daw::app
