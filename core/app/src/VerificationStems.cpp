#include "StemSession.h"
#include "Verification.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/domain/stems/Laying.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <random>

// --verify-stems (S22): the stem separator, in the application, with a real
// model (the fast one), on sources known in advance and mixed here. What it
// proves is in the sound of the stems, never in a field read back:
//   - the separation runs off the message thread: the window keeps ticking;
//   - « Annuler » gives the hand back in under a second, and writes nothing;
//   - the stems are laid on four tracks, the clip gone, one history entry;
//   - each stem is closer to its source than to the mix, and they add up;
//   - one Ctrl+Z gives the project back, to the byte;
//   - separating the same file again comes from the cache, at once.
// Local only: the model is downloaded on first use. The CI proves the same
// protocol with the simulated separator (services/tests/test_stems.py).

namespace daw::app
{
namespace
{

constexpr double rate = 44100.0;
constexpr double seconds = 20.0;
constexpr double bpm = 100.0;
constexpr double separationTimeoutMs = 45.0 * 60.0 * 1000.0; // the first run installs the models

using Signal = std::vector<float>;

double envelope(double t, double attack, double decay)
{
    return std::min(t / attack, 1.0) * std::exp(-t / decay);
}

Signal drums()
{
    const auto frames = static_cast<std::size_t>(seconds * rate);
    Signal out(frames, 0.0f);
    std::mt19937 random{22};
    std::normal_distribution<double> noise{0.0, 1.0};
    const auto beat = static_cast<std::size_t>(rate * 60.0 / bpm);
    for (std::size_t start = 0; start < frames; start += beat)
    {
        const auto kick = (start / beat) % 2 == 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.35 * rate) && start + i < frames; ++i)
        {
            const auto t = static_cast<double>(i) / rate;
            const auto value =
                kick ? 0.9 *
                           std::sin(2.0 * std::numbers::pi * (50.0 * t + 1.8 * (1.0 - std::exp(-t / 0.03)))) *
                           envelope(t, 0.001, 0.12)
                     : (0.5 * noise(random) + 0.3 * std::sin(2.0 * std::numbers::pi * 190.0 * t)) *
                           envelope(t, 0.001, 0.07);
            out[start + i] += static_cast<float>(value);
        }
    }
    for (std::size_t start = 0; start < frames; start += beat / 2)
    {
        double previous = 0.0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(0.05 * rate) && start + i < frames; ++i)
        {
            const auto value = noise(random);
            out[start + i] += static_cast<float>(0.25 * (value - previous) *
                                                 envelope(static_cast<double>(i) / rate, 0.0005, 0.015));
            previous = value;
        }
    }
    return out;
}

Signal bass()
{
    const auto frames = static_cast<std::size_t>(seconds * rate);
    Signal out(frames, 0.0f);
    const auto bar = static_cast<std::size_t>(rate * 4.0 * 60.0 / bpm);
    const std::array<double, 4> roots{55.0, 43.65, 49.0, 41.2};
    for (std::size_t start = 0; start < frames; start += bar / 8)
    {
        const auto f = roots[(start / bar) % 4];
        for (std::size_t i = 0; i < bar / 8 && start + i < frames; ++i)
        {
            const auto t = static_cast<double>(i) / rate;
            double saw = 0.0;
            for (int k = 1; k < 8; ++k)
                saw += std::sin(2.0 * std::numbers::pi * f * k * t) / k;
            out[start + i] += static_cast<float>(0.35 * saw * envelope(t, 0.005, 0.25));
        }
    }
    return out;
}

Signal chords()
{
    const auto frames = static_cast<std::size_t>(seconds * rate);
    Signal out(frames, 0.0f);
    const auto bar = static_cast<std::size_t>(rate * 4.0 * 60.0 / bpm);
    const std::array<std::array<double, 3>, 4> triads{
        {{220.0, 261.6, 329.6}, {174.6, 220.0, 261.6}, {196.0, 246.9, 293.7}, {164.8, 207.7, 246.9}}};
    for (std::size_t start = 0; start < frames; start += bar / 2)
    {
        const auto& triad = triads[(start / bar) % 4];
        for (std::size_t i = 0; i < bar / 2 && start + i < frames; ++i)
        {
            const auto t = static_cast<double>(i) / rate;
            double tone = 0.0;
            for (const auto f : triad)
                for (int k = 1; k < 6; ++k)
                    tone += std::sin(2.0 * std::numbers::pi * f * k * t) * std::pow(0.6, k);
            out[start + i] += static_cast<float>(0.12 * tone * envelope(t, 0.01, 0.8));
        }
    }
    return out;
}

// A voice: Windows' own speech synthesis, French, read in, looped to length.
Signal voice(const juce::File& folder)
{
    const auto file = folder.getChildFile("voix-tts.wav");
    if (!file.existsAsFile())
    {
        const auto script = "Add-Type -AssemblyName System.Speech; $s = New-Object "
                            "System.Speech.Synthesis.SpeechSynthesizer; "
                            "$f = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(44100, "
                            "[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, "
                            "[System.Speech.AudioFormat.AudioChannel]::Mono); $s.SetOutputToWaveFile('" +
                            file.getFullPathName() +
                            "', $f); $s.Rate = -2; $s.Speak('Je chante doucement sur la ville qui dort, les "
                            "lumieres s allument "
                            "une a une, et la nuit descend sur le fleuve. Je chante encore, plus fort, pour "
                            "que tu m entendes de "
                            "loin.'); $s.Dispose()";
        juce::ChildProcess speak;
        if (speak.start(juce::StringArray{"powershell", "-NoProfile", "-Command", script}))
            static_cast<void>(speak.waitForProcessToFinish(60000));
    }

    const auto frames = static_cast<std::size_t>(seconds * rate);
    Signal out(frames, 0.0f);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return out;
    juce::AudioBuffer<float> buffer{1, static_cast<int>(reader->lengthInSamples)};
    static_cast<void>(reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, false));
    for (std::size_t i = 0; i < frames; ++i)
        out[i] = 0.8f *
                 buffer.getSample(0, static_cast<int>(i % static_cast<std::size_t>(buffer.getNumSamples())));
    return out;
}

void write(const juce::File& file, const std::array<Signal, 2>& channels)
{
    juce::AudioBuffer<float> buffer{2, static_cast<int>(channels[0].size())};
    for (int c = 0; c < 2; ++c)
        for (std::size_t i = 0; i < channels[0].size(); ++i)
            buffer.setSample(c, static_cast<int>(i), channels[static_cast<std::size_t>(c)][i]);
    static_cast<void>(file.deleteFile());
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(24));
    if (writer != nullptr)
        static_cast<void>(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}

std::array<Signal, 2> read(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    std::array<Signal, 2> out;
    if (reader == nullptr)
        return out;
    juce::AudioBuffer<float> buffer{2, static_cast<int>(reader->lengthInSamples)};
    static_cast<void>(reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true));
    for (int c = 0; c < 2; ++c)
        out[static_cast<std::size_t>(c)].assign(buffer.getReadPointer(c),
                                                buffer.getReadPointer(c) + buffer.getNumSamples());
    return out;
}

// Signal-to-distortion ratio of `estimate` against `reference`, in dB.
double sdr(const std::array<Signal, 2>& estimate, const std::array<Signal, 2>& reference)
{
    double signal = 0.0;
    double error = 0.0;
    for (std::size_t c = 0; c < 2; ++c)
    {
        const auto frames = std::min(estimate[c].size(), reference[c].size());
        for (std::size_t i = 0; i < frames; ++i)
        {
            signal += static_cast<double>(reference[c][i]) * reference[c][i];
            const auto d = static_cast<double>(reference[c][i]) - estimate[c][i];
            error += d * d;
        }
    }
    return 10.0 * std::log10(signal / std::max(error, 1e-20));
}

std::string fixed(double value, int decimals = 1)
{
    return juce::String(value, decimals).toStdString();
}

struct StemsRun
{
    std::map<std::string, std::array<Signal, 2>> sources;
    std::array<Signal, 2> mix;
    domain::AudioClipId clip;
    std::string before;
    std::size_t depth{0};
    std::size_t tracksBefore{0};
    double cancelledAt{0.0};
    std::size_t laidSeen{0};
};

bool settled(const StemSession& stems)
{
    return stems.stage() == ui::StemHost::Stage::idle || stems.stage() == ui::StemHost::Stage::failed;
}

} // namespace

void Verification::buildStems()
{
    auto run = std::make_shared<StemsRun>();
    if (stems_ == nullptr)
    {
        add("le séparateur est là", [this] { check(false, "une session de séparation dans ce processus"); });
        return;
    }

    add("les sources connues : batterie, basse, accords, une voix ; mélangées, 20 s",
        [this, run]
        {
            const auto folder = folder_.getChildFile("stems");
            static_cast<void>(folder.createDirectory());

            const std::map<std::string, std::pair<Signal, float>> mono{{"drums", {drums(), 0.0f}},
                                                                       {"bass", {bass(), 0.0f}},
                                                                       {"other", {chords(), 0.3f}},
                                                                       {"vocals", {voice(folder), 0.0f}}};
            const auto frames = static_cast<std::size_t>(seconds * rate);
            run->mix = {Signal(frames, 0.0f), Signal(frames, 0.0f)};
            for (const auto& [name, signal] : mono)
            {
                const auto pan = signal.second;
                std::array<Signal, 2> stereo{Signal(frames), Signal(frames)};
                for (std::size_t i = 0; i < frames; ++i)
                {
                    stereo[0][i] = signal.first[i] * (1.0f - pan) / (1.0f + pan);
                    stereo[1][i] = signal.first[i] * (1.0f + pan) / (1.0f + pan);
                }
                run->sources[name] = stereo;
            }
            float peak = 0.0f;
            for (std::size_t i = 0; i < frames; ++i)
                for (std::size_t c = 0; c < 2; ++c)
                {
                    float sum = 0.0f;
                    for (const auto& [name, source] : run->sources)
                        sum += source[c][i];
                    run->mix[c][i] = sum;
                    peak = std::max(peak, std::abs(sum));
                }
            const auto scale = peak > 0.95f ? 0.95f / peak : 1.0f;
            for (auto& [name, source] : run->sources)
                for (auto& channel : source)
                    for (auto& value : channel)
                        value *= scale;
            for (auto& channel : run->mix)
                for (auto& value : channel)
                    value *= scale;

            write(folder.getChildFile("Melange.wav"), run->mix);
            check(sdr(read(folder.getChildFile("Melange.wav")), run->mix) > 60.0,
                  "le mélange écrit se relit");
            const auto voiced = std::any_of(run->sources["vocals"][0].begin(),
                                            run->sources["vocals"][0].end(),
                                            [](float v) { return std::abs(v) > 0.01f; });
            check(voiced, "la voix de la synthèse vocale est là");
        });

    add("le mélange posé en clip, au temps 4, sur sa piste",
        [this, run]
        {
            const auto track = domain::TrackId::generate();
            const auto sample = samples_.import(folder_.getChildFile("stems").getChildFile("Melange.wav"));
            check(sample.ok(), "le mélange entre dans le magasin du projet");
            if (!sample)
                return;
            run->clip = domain::AudioClipId::generate();
            std::vector<std::unique_ptr<domain::Command>> commands;
            commands.push_back(std::make_unique<domain::AddTrack>(track, "Mélange", 0.0));
            commands.push_back(std::make_unique<domain::PlaceAudio>(run->clip, track, sample.value(), 4.0));
            domain::GroupOptions group{};
            group.label = "le mélange";
            check(bus_.executeGroup(std::move(commands), group).ok(), "posé");
            run->before = domain::json::write(state_.toValue());
            run->depth = depth();
            run->tracksBefore = state_.tracks().size();
        });

    add(
        "séparer (rapide) : la progression avance, la fenêtre continue de répondre",
        [this, run]
        {
            longestTickMs_ = 0.0;
            lastTickMs_ = juce::Time::getMillisecondCounterHiRes();
            watchTicks_ = true;
            stems_->separate(run->clip, ui::StemHost::Quality::fast);
            note("état : " + stems_->status());
        },
        [this] { return stems_->progress() > 0.05 || stems_->stage() == ui::StemHost::Stage::failed; },
        separationTimeoutMs);

    add(
        "Annuler : la main revient en moins d'une seconde, rien n'est écrit",
        [this, run]
        {
            watchTicks_ = false;
            note("progression avant l'annulation : " + fixed(stems_->progress() * 100.0, 0) +
                 " %, plus long " + "silence de la fenêtre : " + fixed(longestTickMs_, 0) + " ms");
            check(longestTickMs_ < 250.0, "la fenêtre n'a pas cessé de répondre pendant le calcul");
            run->cancelledAt = juce::Time::getMillisecondCounterHiRes();
            stems_->cancel();
        },
        [this] { return settled(*stems_); },
        5000.0);

    add("…et le projet n'a pas bougé",
        [this, run]
        {
            const auto took = juce::Time::getMillisecondCounterHiRes() - run->cancelledAt;
            note("rendu la main en " + fixed(took, 0) + " ms");
            check(took < 1000.0, "sous une seconde");
            check(domain::json::write(state_.toValue()) == run->before, "le projet, à l'octet");
            check(depth() == run->depth, "aucune entrée d'historique");
            run->laidSeen = stems_->lastLaid().has_value() ? 1 : 0;
        });

    // S26: the separator's process killed during a separation, as a crash of
    // it would end it. The DAW says it and writes nothing; the separation
    // below, asked again, starts it again.
    add(
        "séparer, puis le processus du séparateur tué",
        [this, run]
        {
            run->before = domain::json::write(state_.toValue());
            run->depth = depth();
            stems_->separate(run->clip, ui::StemHost::Quality::fast);
        },
        [this] { return stems_->progress() > 0.05 || stems_->stage() == ui::StemHost::Stage::failed; },
        separationTimeoutMs);
    add("…le DAW le dit", [this] { stems_->killForTest(); }, [this] { return settled(*stems_); }, 10000.0);
    add("…et le projet n'a pas bougé, sans piste ni entrée d'historique",
        [this, run]
        {
            check(stems_->stage() == ui::StemHost::Stage::failed && !stems_->status().empty(),
                  "« " + stems_->status() + " »");
            check(domain::json::write(state_.toValue()) == run->before, "le projet, à l'octet");
            check(depth() == run->depth, "aucune entrée d'historique");
        });

    add(
        "séparer de nouveau, jusqu'au bout",
        [this, run] { stems_->separate(run->clip, ui::StemHost::Quality::fast); },
        [this] { return stems_->lastLaid().has_value() || stems_->stage() == ui::StemHost::Stage::failed; },
        separationTimeoutMs);

    add("quatre pistes nommées et rôlées, le clip parti, une seule entrée d'historique",
        [this, run]
        {
            check(stems_->stage() != ui::StemHost::Stage::failed, "pas d'échec : " + stems_->status());
            const auto& laid = stems_->lastLaid();
            if (!laid.has_value())
                return;
            note("séparé en " + fixed(laid->seconds) + " s" + (laid->fromCache ? " (cache)" : ""));
            check(state_.tracks().size() == run->tracksBefore + 4, "quatre pistes de plus");
            check(state_.findAudioClip(run->clip) == nullptr, "le clip du mélange est retiré");
            check(depth() == run->depth + 1, "une seule entrée d'historique");
            for (std::size_t index = 0; index < laid->tracks.size(); ++index)
            {
                const auto* track = state_.findTrack(laid->tracks[index]);
                const auto name = std::string{domain::stems::names[index]};
                check(track != nullptr && track->role == domain::stems::roleOf(name),
                      name + " : rôle « " +
                          (track != nullptr && track->role ? std::string{domain::mixRoleName(*track->role)}
                                                           : "aucun") +
                          " »");
            }
            snapshot("s22-stems-poses");
        });

    add("chaque stem est plus proche de sa source que du mélange, et leur somme rend le mélange",
        [this, run]
        {
            const auto& laid = stems_->lastLaid();
            if (!laid.has_value())
                return;
            std::array<Signal, 2> total{Signal(run->mix[0].size(), 0.0f), Signal(run->mix[1].size(), 0.0f)};
            for (const auto name : domain::stems::names)
            {
                const auto file = laid->files.find(std::string{name});
                if (file == laid->files.end())
                {
                    check(false, std::string{name} + " : écrit");
                    continue;
                }
                const auto stem = read(file->second);
                const auto toSource = sdr(stem, run->sources[std::string{name}]);
                const auto toMix = sdr(stem, run->mix);
                note(std::string{name} + " : " + fixed(toSource) + " dB vers sa source, " + fixed(toMix) +
                     " dB vers le mélange");
                check(toSource > toMix, std::string{name} + " plus proche de sa source que du mélange");
                for (std::size_t c = 0; c < 2; ++c)
                    for (std::size_t i = 0; i < std::min(stem[c].size(), total[c].size()); ++i)
                        total[c][i] += stem[c][i];
            }
            const auto adds = sdr(total, run->mix);
            note("somme des stems vers le mélange : " + fixed(adds) + " dB");
            check(adds > 40.0, "la somme rend le mélange");
        });

    add("Ctrl+Z : le projet d'avant la séparation, à l'octet",
        [this, run]
        {
            check(bus_.undo().ok(), "annulé");
            check(domain::json::write(state_.toValue()) == run->before, "le projet, à l'octet");
        });

    add(
        "séparer le même fichier encore : depuis le cache, tout de suite",
        [this, run] { stems_->separate(run->clip, ui::StemHost::Quality::fast); },
        [this, run]
        {
            const auto& laid = stems_->lastLaid();
            return (laid.has_value() && state_.findAudioClip(run->clip) == nullptr) ||
                   stems_->stage() == ui::StemHost::Stage::failed;
        },
        60000.0);

    add("…posé depuis le cache, et un Ctrl+Z le retire encore",
        [this, run]
        {
            const auto& laid = stems_->lastLaid();
            check(laid.has_value() && laid->fromCache, "les stems viennent du cache");
            if (laid.has_value())
            {
                note("en " + fixed(laid->seconds, 2) + " s");
                check(laid->seconds < 5.0, "en moins de 5 s");
            }
            check(bus_.undo().ok(), "annulé");
            check(domain::json::write(state_.toValue()) == run->before, "le projet, à l'octet");
        });
}

} // namespace daw::app
