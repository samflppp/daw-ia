#include "MixSession.h"
#include "Verification.h"
#include "VerificationTiming.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/panels/HistoryPanel.h"
#include "daw/ui/panels/MixerPanel.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>

// --verify-mix (S20): the mix by the AI, run on signals whose measures are
// known in advance, by the rules only — no key, no API, the same in CI as on
// this machine. Measuring is checked against numbers a calculator gives;
// deciding against the guards; the result against what was rendered, never
// against what the state says.

namespace daw::app
{
namespace
{

using Stage = ui::MixHost::Stage;

constexpr double signalRate = 44100.0;
constexpr double signalSeconds = 20.0;
constexpr double beatSeconds = 0.5; // 120 BPM, the tempo of a new project
constexpr double mixTimeoutMs = 180000.0;

// Stereo, 24 bits, the same signal on both sides.
void writeSignal(const juce::File& file, const std::function<double(double)>& at)
{
    juce::AudioBuffer<float> buffer{2, static_cast<int>(signalSeconds * signalRate)};
    for (int index = 0; index < buffer.getNumSamples(); ++index)
    {
        const auto value = static_cast<float>(at(static_cast<double>(index) / signalRate));
        buffer.setSample(0, index, value);
        buffer.setSample(1, index, value);
    }
    static_cast<void>(file.deleteFile());
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(signalRate).withNumChannels(2).withBitsPerSample(24));
    if (writer != nullptr)
        static_cast<void>(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}

double sine(double frequency, double seconds)
{
    return std::sin(2.0 * std::numbers::pi * frequency * seconds);
}

std::string fixed(double value, int decimals = 2)
{
    return juce::String(value, decimals).toStdString();
}

// How bright a master is: its top four octaves (2 to 16 kHz) against its
// bottom four (31.5 to 250 Hz), in dB.
double brightness(const domain::mix::StreamMeasure& master)
{
    double top = 0.0;
    double bottom = 0.0;
    for (std::size_t band = 0; band < 4; ++band)
    {
        bottom += master.bandsDb[band] / 4.0;
        top += master.bandsDb[domain::mix::bandCount - 1 - band] / 4.0;
    }
    return top - bottom;
}

bool settled(const MixSession& mix)
{
    return mix.stage() == Stage::ready || mix.stage() == Stage::failed || mix.stage() == Stage::idle;
}

// What the run carries from one step to the next.
struct MixRun
{
    domain::TrackId kick;
    domain::TrackId bass;
    domain::TrackId chords;
    domain::TrackId lead;
    std::string projectBefore;
    std::size_t depthBefore{0};
    double afterMasterLufs{0.0};
    double trimDb{0.0};
    std::map<std::string, double> afterTracks;
    double cancelMs{0.0};
    double brightWithout{0.0};
    std::string kickBefore;
};

} // namespace

void Verification::buildMix()
{
    auto run = std::make_shared<MixRun>();
    if (mix_ == nullptr)
    {
        add("le mixage est là", [this] { check(false, "une session de mixage dans ce processus"); });
        return;
    }
    mix_->setUseModel(false);

    add("le projet de mesure : quatre signaux connus, deux rôles dits",
        [this, run]
        {
            const auto folder = folder_.getChildFile("signaux");
            static_cast<void>(folder.createDirectory());

            // A kick on every beat, 60 Hz falling 36 dB in half a second; a
            // bass holding 60 Hz under it: the two share the 63 Hz octave.
            // Chords at 250 and 500 Hz. A lead, 1 kHz at -20 dBFS: -20 LUFS
            // and -20 dBTP, by the definition of both.
            writeSignal(folder.getChildFile("Kick.wav"),
                        [](double t)
                        {
                            const auto since = std::fmod(t, beatSeconds);
                            return 0.9 * std::exp(-since / 0.12) * sine(60.0, since);
                        });
            writeSignal(folder.getChildFile("Basse.wav"), [](double t) { return 0.5 * sine(60.0, t); });
            writeSignal(folder.getChildFile("Accords.wav"),
                        [](double t) { return 0.2 * sine(250.0, t) + 0.2 * sine(500.0, t); });
            writeSignal(folder.getChildFile("Lead.wav"), [](double t) { return 0.1 * sine(1000.0, t); });

            const auto place = [this, &folder](const std::string& name) -> domain::TrackId
            {
                const auto id = domain::TrackId::generate();
                if (!bus_.execute(std::make_unique<domain::AddTrack>(id, name, 0.0)).ok())
                    return id;
                const auto sample = samples_.import(folder.getChildFile(name + ".wav"));
                if (sample)
                    static_cast<void>(bus_.execute(std::make_unique<domain::PlaceAudio>(
                        domain::AudioClipId::generate(), id, sample.value(), 0.0)));
                return id;
            };
            run->kick = place("Kick");
            run->bass = place("Basse");
            run->chords = place("Accords");
            run->lead = place("Lead");
            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetTrackRole>(run->kick, domain::MixRole::kick)));
            static_cast<void>(
                bus_.execute(std::make_unique<domain::SetTrackRole>(run->bass, domain::MixRole::bass)));

            check(state_.tracks().size() >= 4, "quatre pistes");
            check(state_.audioClips().size() == 4, "quatre clips audio");
        });

    add(
        "Mixer : la mesure, la décision par les règles, l'essai à blanc",
        [this, run]
        {
            run->projectBefore = domain::json::write(state_.toValue());
            run->depthBefore = depth();
            mix_->start();
            check(mix_->stage() == Stage::measuring, "la mesure commence");
        },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("les mesures des signaux connus donnent les nombres attendus",
        [this, run]
        {
            note("état : " + mix_->status());
            check(mix_->stage() == Stage::ready, "une proposition est prête");
            const auto* before = mix_->before();
            if (before == nullptr)
            {
                check(false, "la mesure est là");
                return;
            }
            note("mesure en " + fixed(mix_->measureSeconds(), 1) + " s, copie de l'Edit " +
                 fixed(mix_->prepareMs(), 0) + " ms sur le fil des messages");

            const auto& master = before->master;
            check(std::abs(master.seconds - signalSeconds) < 0.01,
                  "le master dure 20 s (" + fixed(master.seconds, 3) + ")");

            const auto lead = before->tracks.find(run->lead.toString());
            if (lead == before->tracks.end())
            {
                check(false, "le lead est mesuré");
                return;
            }
            const auto& tone = lead->second;
            check(std::abs(tone.integratedLufs + 20.0) < 0.2,
                  "lead 1 kHz à -20 dBFS : -20 LUFS (" + fixed(tone.integratedLufs) + ")");
            check(std::abs(tone.truePeakDb + 20.0) < 0.2, "et -20 dBTP (" + fixed(tone.truePeakDb) + ")");
            check(std::abs(tone.bandsDb[5] + 23.01) < 0.5,
                  "l'octave 1 kHz à -23 dB, une sinusoïde d'amplitude 0,1 (" + fixed(tone.bandsDb[5]) + ")");
            check(tone.bandsDb[5] > tone.bandsDb[3] + 30.0, "rien dans l'octave 250 Hz");
            check(tone.activeShare > 0.95, "le lead joue tout le morceau");

            const auto bass = before->tracks.find(run->bass.toString());
            if (bass != before->tracks.end())
                check(std::abs(bass->second.bandsDb[1] + 9.03) < 0.5,
                      "basse 60 Hz d'amplitude 0,5 : -9 dB dans l'octave 63 Hz (" +
                          fixed(bass->second.bandsDb[1]) + ")");
            check(master.truePeakDb > 0.0,
                  "le master avant mixage dépasse 0 dBTP (" + fixed(master.truePeakDb) + ")");
        });

    add("la proposition passe les garde-fous, chaque réglage cite une vraie mesure",
        [this, run]
        {
            const auto* proposal = mix_->proposal();
            const auto* brief = mix_->brief();
            if (proposal == nullptr || brief == nullptr)
            {
                check(false, "une proposition et son brief");
                return;
            }
            check(proposal->decidedBy == "règles",
                  "décidée par les règles, sans clé (" + proposal->decidedBy + ")");
            check(!proposal->changes.empty(),
                  std::to_string(proposal->changes.size()) + " réglages proposés");
            check(mix_->refused().empty(), "rien de refusé par les garde-fous");
            check(domain::mix::check(*brief, *proposal).empty(),
                  "les garde-fous relus sur ce qui est montré : rien ne les franchit");
            for (const auto& change : proposal->changes)
                note("« " + change.sentence + " »");

            // What a model would read and write, in characters: the cost of a
            // mix by the model is estimated from them (about 3,5 characters a
            // token for this JSON), never measured here, where no API is called.
            const auto read = domain::json::write(brief->toValue()).size();
            const auto written = domain::json::write(proposal->toValue()).size();
            note("brief : " + std::to_string(read) + " caractères pour " +
                 std::to_string(brief->strips.size()) + " pistes ; proposition : " + std::to_string(written) +
                 " caractères");

            const auto* kick = brief->find(run->kick);
            const auto* bass = brief->find(run->bass);
            check(kick != nullptr && kick->role.chosen && kick->role.role == domain::MixRole::kick,
                  "le kick est le kick que la personne a dit");
            check(bass != nullptr && bass->role.chosen && bass->role.role == domain::MixRole::bass,
                  "la basse aussi");

            // S21: the bass is carved on the kick's margin on its hits, and the
            // sentence cites that margin, never an overlap of the two.
            bool citesMargin = false;
            bool citesOverlap = false;
            for (const auto& change : proposal->changes)
            {
                if (change.track != run->bass || change.kind != domain::mix::Change::Kind::equaliser)
                    continue;
                for (const auto& cited : change.evidence)
                {
                    citesMargin = citesMargin || cited.measure == "margin.1." + run->kick.toString();
                    citesOverlap = citesOverlap || cited.measure.starts_with("overlap.");
                }
            }
            if (!brief->margins.empty())
                note(
                    "marge du kick sur ses coups, dans le brief : " + fixed(brief->margins.front().marginDb) +
                    " dB à " + domain::mix::bandName(brief->margins.front().band));
            check(citesMargin && !citesOverlap,
                  "la basse est creusée sur la marge du kick, et la phrase la cite");
            const auto* lead = brief->find(run->lead);
            if (lead != nullptr)
                note("le lead deviné : " + domain::mix::roleLabel(lead->role.role) + ", " +
                     lead->role.because);
        });

    add("l'essai rendu : le master sous -1 dBTP, le kick et la basse se masquent moins",
        [this, run]
        {
            const auto* before = mix_->before();
            const auto* after = mix_->after();
            if (before == nullptr || after == nullptr)
            {
                check(false, "l'avant et l'après sont rendus");
                return;
            }
            const auto trim = mix_->masterTrimDb();
            run->trimDb = trim.has_value() ? *trim - state_.master().volumeDb : 0.0;
            const auto peak = after->master.truePeakDb + run->trimDb;
            note("master : " + fixed(before->master.integratedLufs) + " LUFS avant, " +
                 fixed(after->master.integratedLufs) + " LUFS après l'essai ; crête vraie " +
                 fixed(after->master.truePeakDb) + " dBTP, correction du master " + fixed(run->trimDb) +
                 " dB");
            if (!mix_->masterSentence().empty())
                note("« " + mix_->masterSentence() + " »");
            check(peak <= -1.0 + 1e-6, "le master gardé reste sous -1 dBTP (" + fixed(peak) + ")");

            // As heard: each track through its fader, before and after.
            const auto* proposal = mix_->proposal();
            const auto fader = [this, proposal](const domain::TrackId& track, bool after)
            {
                auto volume = state_.findTrack(track) != nullptr ? state_.findTrack(track)->volumeDb : 0.0;
                if (after && proposal != nullptr)
                {
                    for (const auto* change : proposal->of(track))
                    {
                        if (change->kind == domain::mix::Change::Kind::volume)
                            volume = change->value;
                    }
                }
                return volume;
            };
            const auto pair = [&fader](const engine::MixRender::Measured& measured,
                                       const domain::TrackId& first,
                                       const domain::TrackId& second,
                                       bool after)
            {
                std::vector<domain::mix::StreamMeasure> streams{
                    domain::mix::gained(measured.tracks.at(first.toString()), fader(first, after)),
                    domain::mix::gained(measured.tracks.at(second.toString()), fader(second, after))};
                double worst = 0.0;
                for (const auto& overlap : domain::mix::overlaps(streams))
                    worst = std::max(worst, overlap.share);
                return worst;
            };
            const auto overlapBefore = pair(*before, run->kick, run->bass, false);
            const auto overlapAfter = pair(*after, run->kick, run->bass, true);
            note("recouvrement kick/basse, à travers les faders : " + fixed(overlapBefore * 100.0, 0) +
                 " % avant, " + fixed(overlapAfter * 100.0, 0) +
                 " % après (le critère à 6 dB ne voit pas le niveau d'un kick qui décroît : chaque coup "
                 "traverse la basse, quel que soit son niveau)");

            // The criterion since S21, the one the rules read and the sentence
            // cites: the kick passes over the bass where it hits. Its 63 Hz
            // octave against the bass's, through the faders, on the fifth of
            // the hops where the kick is loudest (domain::mix::hitMarginDb).
            const auto margin = [&fader](const engine::MixRender::Measured& measured,
                                         const domain::TrackId& kick,
                                         const domain::TrackId& bass,
                                         bool after)
            {
                const auto hit = domain::mix::gained(measured.tracks.at(kick.toString()), fader(kick, after));
                const auto under =
                    domain::mix::gained(measured.tracks.at(bass.toString()), fader(bass, after));
                return domain::mix::hitMarginDb(hit, under, 1).value_or(0.0);
            };
            const auto marginBefore = margin(*before, run->kick, run->bass, false);
            const auto marginAfter = margin(*after, run->kick, run->bass, true);
            note("sur ses coups, le kick passe la basse à 63 Hz de " + fixed(marginBefore) + " dB avant, " +
                 fixed(marginAfter) + " dB après");
            check(marginAfter >= marginBefore + 3.0,
                  "le kick passe au-dessus de la basse d'au moins 3 dB de plus");

            run->afterMasterLufs = after->master.integratedLufs;
            for (const auto& [key, measure] : after->tracks)
                run->afterTracks[key] = measure.integratedLufs;

            auto* comparison = mix_->comparison();
            if (comparison != nullptr)
            {
                const auto difference = after->master.integratedLufs - before->master.integratedLufs;
                note("écoute à niveau égal : avant " + fixed(comparison->beforeGainDb()) + " dB, après " +
                     fixed(comparison->afterGainDb()) + " dB");
                check(std::abs(comparison->beforeGainDb() + comparison->afterGainDb() +
                               std::abs(difference)) < 0.01 &&
                          (comparison->beforeGainDb() == 0.0 || comparison->afterGainDb() == 0.0),
                      "le plus fort des deux est baissé de l'écart mesuré, l'autre ne bouge pas");
            }
            check(depth() == run->depthBefore, "rien n'est écrit tant que la personne n'a pas gardé");
            check(domain::json::write(state_.toValue()) == run->projectBefore, "le projet n'a pas bougé");
        });

    add(
        "écouter l'avant",
        [this]
        {
            if (mix_->comparison() != nullptr)
                mix_->comparison()->stop();
            mix_->listen(false);
            check(!mix_->listeningAfter(), "l'avant joue");
        },
        [this] { return mix_->comparison() == nullptr || mix_->comparison()->heardRmsDb(false) > -60.0; },
        6000.0);

    add(
        "basculer sur l'après, à la même position",
        [this] { mix_->listen(true); },
        [this]
        {
            return mix_->comparison() == nullptr || (mix_->comparison()->heardRmsDb(true) > -60.0 &&
                                                     mix_->comparison()->heardRmsDb(false) > -60.0);
        },
        6000.0);

    add("l'avant et l'après s'entendent au même niveau",
        [this]
        {
            auto* comparison = mix_->comparison();
            if (comparison == nullptr)
            {
                note("pas de carte son : l'écoute n'est pas vérifiée");
                return;
            }
            check(mix_->listeningAfter(), "l'après joue");
            const auto heardBefore = comparison->heardRmsDb(false);
            const auto heardAfter = comparison->heardRmsDb(true);
            note("entendu : avant " + fixed(heardBefore) + " dB RMS, après " + fixed(heardAfter) + " dB RMS");
            check(std::abs(heardBefore - heardAfter) < 1.5, "à 1,5 dB près");
            comparison->stop();
        });

    add("garder : une seule entrée d'historique, par le copilote",
        [this, run]
        {
            mix_->accept();
            note("état : " + mix_->status());
            check(depth() == run->depthBefore + 1, "une seule entrée d'historique");
            const auto& entries = history_.entries();
            const auto* last = history_.cursor() > 0 ? &entries[history_.cursor() - 1] : nullptr;
            check(last != nullptr && last->actor == domain::Actor::copilot, "marquée copilote");
            check(last != nullptr && std::string{last->label()}.starts_with("Mixage par l'IA"),
                  "nommée « Mixage par l'IA » (" + (last != nullptr ? std::string{last->label()} : "") + ")");
            check(domain::json::write(state_.toValue()) != run->projectBefore, "le projet a changé");
        });

    add(
        "le mixage écrit sonne comme l'essai : mesuré de nouveau, rendu à l'appui",
        [this] { mix_->start(); },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("les mesures du projet mixé sont celles de l'essai",
        [this, run]
        {
            const auto* now = mix_->before();
            check(!mix_->measureReused(), "le projet a changé : il est mesuré de nouveau");
            if (now == nullptr)
            {
                check(false, "la mesure est là");
                return;
            }
            const auto expected = run->afterMasterLufs + run->trimDb;
            check(std::abs(now->master.integratedLufs - expected) < 0.1,
                  "le master : " + fixed(now->master.integratedLufs) +
                      " LUFS, l'essai et la correction disaient " + fixed(expected));
            double worst = 0.0;
            for (const auto& [key, lufs] : run->afterTracks)
            {
                if (const auto found = now->tracks.find(key); found != now->tracks.end())
                    worst = std::max(worst, std::abs(found->second.integratedLufs - lufs));
            }
            check(worst < 0.1, "chaque piste comme à l'essai, à " + fixed(worst, 3) + " LU près");
            mix_->reject();
        });

    add("Ctrl+Z défait le mixage, à l'octet près",
        [this, run]
        {
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(depth() == run->depthBefore, "l'entrée est défaite");
            check(domain::json::write(state_.toValue()) == run->projectBefore,
                  "le projet est celui d'avant, octet pour octet");
        });

    add(
        "refuser une piste : relancer",
        [this] { mix_->start(); },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("la piste refusée n'est pas touchée, les autres le sont",
        [this, run]
        {
            if (mix_->proposal() == nullptr)
            {
                check(false, "une proposition");
                return;
            }
            const auto kickBefore = domain::json::write(state_.findTrack(run->kick)->toValue());
            const auto touchesKick = !mix_->proposal()->of(run->kick).empty();
            note(std::string{"la proposition touche le kick : "} + (touchesKick ? "oui" : "non"));
            mix_->refuseTrack(run->kick, true);
            check(mix_->isRefused(run->kick), "le kick est refusé");
            mix_->accept();
            check(depth() == run->depthBefore + 1, "une entrée");
            check(domain::json::write(state_.findTrack(run->kick)->toValue()) == kickBefore,
                  "le kick n'a pas bougé");
            check(domain::json::write(state_.toValue()) != run->projectBefore, "le reste a bougé");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == run->projectBefore, "Ctrl+Z, à l'octet près");
        });

    add(
        "refuser tout : rien n'est écrit, la mesure resservie",
        [this] { mix_->start(); },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("refusé",
        [this, run]
        {
            check(mix_->measureReused(), "rien n'a changé depuis : la mesure est reprise, pas refaite");
            mix_->reject();
            check(mix_->stage() == Stage::idle, "plus de proposition");
            check(depth() == run->depthBefore, "aucune entrée");
            check(domain::json::write(state_.toValue()) == run->projectBefore, "le projet n'a pas bougé");
        });

    add("annuler pendant la mesure : vite, et rien d'écrit",
        [this, run]
        {
            // Something that sounds changes: the measure must be made again.
            static_cast<void>(bus_.execute(std::make_unique<domain::SetTrackVolume>(run->lead, -3.0)));
            mix_->start();
            check(mix_->stage() == Stage::measuring, "la mesure commence");
            const auto started = juce::Time::getMillisecondCounterHiRes();
            mix_->cancel();
            run->cancelMs = juce::Time::getMillisecondCounterHiRes() - started;
            note("annulation en " + fixed(run->cancelMs, 0) + " ms");
            check(run->cancelMs < 1000.0, "en moins d'une seconde");
            check(mix_->stage() == Stage::idle, "plus rien en cours");
            check(depth() == run->depthBefore + 1, "seul le volume du lead est dans l'historique");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == run->projectBefore, "et il est défait");
        });

    add(
        "une référence : un morceau mesuré comme le master",
        [this]
        {
            const auto reference = folder_.getChildFile("signaux").getChildFile("Référence.wav");
            writeSignal(reference,
                        [](double t)
                        { return 0.3 * sine(60.0, t) + 0.3 * sine(4000.0, t) + 0.1 * sine(1000.0, t); });
            mix_->setReference(reference.getFullPathName().toStdString());
        },
        [this] { return mix_->stage() == Stage::idle && !mix_->reference().empty(); },
        30000.0);

    add(
        "Mixer vers la référence",
        [this]
        {
            note("état : " + mix_->status());
            mix_->start();
        },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("la cible est la référence, la proposition passe les garde-fous",
        [this]
        {
            const auto* brief = mix_->brief();
            check(mix_->stage() == Stage::ready, "une proposition");
            // Since S22 the reference is read into the project's direction,
            // and the mix aims at the direction.
            check(brief != nullptr && brief->target.source == "direction : Référence.wav",
                  "la cible vient de la direction, qui tient la référence (" +
                      (brief != nullptr ? brief->target.source : "") + ")");
            check(brief != nullptr && brief->target.tilt.has_value(), "avec sa pente spectrale");
            check(mix_->refused().empty(), "rien de refusé");
            mix_->reject();
            mix_->clearReference();
        });

    // The direction at its effect (S22): the same song mixed without one, then
    // towards a dark reference, all the way; the second master is measured
    // darker. The rules decide both: no key.
    add(
        "sans direction : Mixer, et mesurer la brillance du master à l'essai",
        [this] { mix_->start(); },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add(
        "une référence sombre, la direction à fond",
        [this, run]
        {
            check(mix_->stage() == Stage::ready && mix_->after() != nullptr,
                  "une proposition sans direction");
            if (mix_->after() != nullptr)
                run->brightWithout = brightness(mix_->after()->master);
            note("brillance sans direction : " + fixed(run->brightWithout, 1) + " dB");
            mix_->reject();

            const auto dark = folder_.getChildFile("signaux").getChildFile("Sombre.wav");
            writeSignal(dark,
                        [](double t)
                        { return 0.5 * sine(60.0, t) + 0.3 * sine(125.0, t) + 0.01 * sine(4000.0, t); });
            mix_->setReference(dark.getFullPathName().toStdString());
        },
        [this] { return mix_->reference().find("Sombre.wav") != std::string::npos; },
        60000.0);

    add(
        "Mixer vers elle",
        [this]
        {
            mix_->setReferenceAmount(1.0);
            mix_->start();
        },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("le master à l'essai est plus sombre que sans direction, et un Ctrl+Z retire la direction",
        [this, run]
        {
            check(mix_->stage() == Stage::ready && mix_->after() != nullptr,
                  "une proposition vers la référence");
            if (mix_->after() == nullptr)
                return;
            const auto with = brightness(mix_->after()->master);
            note("brillance vers la référence sombre : " + fixed(with, 1) +
                 " dB, sans : " + fixed(run->brightWithout, 1) + " dB");
            check(with < run->brightWithout - 0.5, "plus sombre d'au moins 0,5 dB");
            mix_->reject();
            mix_->clearReference();
            check(state_.direction().empty(), "la direction est retirée");
        });

    add(
        "le copilote lance le mixage avec un axe",
        [this, run]
        {
            // What is in the history before: the references of the direction
            // were written on purpose (S22); the copilot's mix writes nothing.
            run->depthBefore = depth();
            const auto said = mix_->startFromCopilot(domain::Value::object({{"punch", domain::Value{0.5}}}));
            const auto started = said.boolAt("started");
            check(started && started.value(), "lancé");
        },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("l'axe est dans le brief, rien n'est écrit",
        [this, run]
        {
            const auto* brief = mix_->brief();
            check(brief != nullptr && std::abs(brief->axes.punch - 0.5) < 1e-9, "punch à 0,5");
            check(mix_->stage() == Stage::ready, "une proposition attend la personne");
            check(depth() == run->depthBefore, "rien n'est écrit");
            mix_->reject();
            mix_->setAxes(domain::mix::Axes{});
        });

    // --- the same, by the mixer, as a person does it --------------------------

    add("F10 : le mixer", [this] { key(juce::KeyPress{juce::KeyPress::F10Key}); });

    add("la page Mixer est ouverte",
        [this]
        {
            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            check(mixer != nullptr && mixer->isShowing(), "la page Mixer est à l'écran");
        });

    add(
        "« Mixer » dans le mixer : la proposition prend la place des tranches",
        [this, run]
        {
            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            auto* ask = mixer != nullptr ? button(*mixer, "Mixer") : nullptr;
            if (ask == nullptr)
            {
                check(false, "le bouton « Mixer »");
                return;
            }
            run->depthBefore = depth();
            run->projectBefore = domain::json::write(state_.toValue());
            click(*ask, ask->getLocalBounds().getCentre());
        },
        [this] { return settled(*mix_); },
        mixTimeoutMs);

    add("une ligne par tranche, sa phrase ; le repeint du mixer avec la proposition",
        [this, run]
        {
            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            if (mixer == nullptr)
                return;
            auto& shown = mixer->proposal();
            check(shown.isVisible() && !mixer->strips().back()->isVisible(),
                  "la proposition est à l'écran, à la place des tranches");
            check(shown.rowCount() >= 4, std::to_string(shown.rowCount()) + " lignes");
            check(shown.rowToggle(run->kick) != nullptr && shown.rowToggle(run->kick)->getToggleState(),
                  "le kick est coché : gardé par défaut");
            check(shown.shownSentences().contains("LUFS"), "les phrases sont à l'écran");
            const auto timed = timing::measure(*mixer, juce::NativeImageType{}, 5, 30);
            note("repeint du mixer avec la proposition (Direct2D) : " + timing::describe(timed));
            check(timed.p95 < 16.0, "sous 16 ms au 95e centile");
        });

    add("décocher le kick, « Garder » : une entrée, le kick intact",
        [this, run]
        {
            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            if (mixer == nullptr)
                return;
            auto& shown = mixer->proposal();
            run->kickBefore = domain::json::write(state_.findTrack(run->kick)->toValue());
            if (auto* toggle = shown.rowToggle(run->kick); toggle != nullptr)
                click(*toggle, toggle->getLocalBounds().getCentre());
            check(mix_->isRefused(run->kick), "le kick est refusé d'un clic");
            if (auto* keep = button(shown, "Garder"); keep != nullptr)
                click(*keep, keep->getLocalBounds().getCentre());
            check(depth() == run->depthBefore + 1, "une entrée");
            check(domain::json::write(state_.findTrack(run->kick)->toValue()) == run->kickBefore,
                  "le kick n'a pas bougé");
        });

    add("les tranches reviennent ; Ctrl+Z à l'octet",
        [this, run]
        {
            auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            if (mixer == nullptr)
                return;
            check(!mixer->proposal().isVisible() && !mixer->strips().empty() &&
                      mixer->strips().back()->isVisible(),
                  "les tranches sont revenues");

            // The history line says what was done, sentence by sentence.
            auto* history = dynamic_cast<ui::HistoryPanel*>(panel("history"));
            const auto said = history != nullptr ? history->sentencesAtRow(0) : juce::String{};
            note("survol de la ligne d'historique :\n\n```\n" + said.toStdString() + "\n```");
            check(said.contains("LUFS") && !said.contains("Kick jouait"),
                  "l'historique redit les phrases gardées, sans celles du kick laissé de côté");
            key(juce::KeyPress{'z', juce::ModifierKeys::ctrlModifier, 0});
            check(domain::json::write(state_.toValue()) == run->projectBefore,
                  "le projet d'avant, à l'octet près");
        });
}

} // namespace daw::app
