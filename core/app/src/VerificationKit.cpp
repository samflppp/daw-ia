#include "KitSession.h"
#include "Verification.h"
#include "daw/domain/commands/DirectionCommands.h"
#include "daw/domain/kit/Choice.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/panels/KitPanel.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <numbers>
#include <random>
#include <string>
#include <thread>
#include <vector>

// --verify-kit (S24): the kit, on a library built here, never the person's:
// sounds whose measures are known, laid out the way a sample pack is. The
// index is built, read back, the kit chosen by its constraints, listened to,
// laid down in one group and taken back by one Ctrl+Z; then what the index
// costs on ten thousand files.

namespace daw::app
{
namespace
{

constexpr double rate = 48000.0;
constexpr std::size_t bigLibrary = 10000;

void writeWav(const juce::File& file, const std::vector<float>& samples, int bits = 24)
{
    static_cast<void>(file.getParentDirectory().createDirectory());
    static_cast<void>(file.deleteFile());
    juce::AudioBuffer<float> buffer{1, static_cast<int>(samples.size())};
    buffer.copyFrom(0, 0, samples.data(), static_cast<int>(samples.size()));
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(bits));
    if (writer != nullptr)
        static_cast<void>(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}

// A tone gliding from `from` to `to` Hz in `glide` s, fading by `decay` dB/s.
std::vector<float>
tone(double from, double to, double glide, double seconds, double decay, double level = 0.8)
{
    std::vector<float> samples(static_cast<std::size_t>(seconds * rate));
    double phase = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto t = static_cast<double>(index) / rate;
        const auto hz = t < glide ? from * std::pow(to / from, t / glide) : to;
        phase += 2.0 * std::numbers::pi * hz / rate;
        samples[index] = static_cast<float>(level * std::pow(10.0, -decay * t / 20.0) * std::sin(phase));
    }
    return samples;
}

// Noise, a first difference of it (bright) or not, fading.
std::vector<float> noise(double seconds, double decay, unsigned seed, int differences)
{
    std::vector<float> samples(static_cast<std::size_t>(seconds * rate));
    std::mt19937 random{seed};
    std::uniform_real_distribution<float> uniform{-0.5f, 0.5f};
    float previous = 0.0f;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        const auto value = uniform(random);
        auto shaped = value;
        if (differences > 0)
            shaped = value - previous;
        previous = value;
        samples[index] =
            static_cast<float>(shaped * std::pow(10.0, -decay * static_cast<double>(index) / rate / 20.0));
    }
    return samples;
}

std::string fixedOf(double value, int decimals = 2)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.*f", decimals, value);
    return text.data();
}

std::string fileOf(const std::string& path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

struct Verification::KitRun
{
    juce::File library;
    juce::File big;
    std::map<std::string, std::string> expectedRoles; // file name, role token
    std::vector<std::string> firstKit;
    std::string projectBefore;
    std::size_t depthBefore{0};
    std::size_t tracksBefore{0};
    juce::Time personalIndexBefore;
    bool personalIndexExisted{false};
};

void Verification::buildKit()
{
    auto run = std::make_shared<KitRun>();
    const auto window = [this]() -> ui::KitPanel* { return dynamic_cast<ui::KitPanel*>(panel("kit")); };
    const auto settled = [this]
    { return kitSession_ != nullptr && kitSession_->stage() != ui::KitHost::Stage::indexing; };

    add("une bibliothèque construite : des 808, des kicks, des caisses claires, des charleys, des "
        "percussions",
        [this, run]
        {
            if (kitSession_ == nullptr)
            {
                check(false, "la session du kit est branchée");
                return;
            }
            const auto personal = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                      .getChildFile("DAW IA/samples-index.json");
            run->personalIndexExisted = personal.existsAsFile();
            run->personalIndexBefore = personal.getLastModificationTime();

            run->library = folder_.getChildFile("bibliotheque");
            static_cast<void>(run->library.deleteRecursively());
            const auto put = [&](const std::string& path, const std::vector<float>& sound, const char* role)
            {
                writeWav(run->library.getChildFile(juce::String::fromUTF8(path.c_str())), sound);
                run->expectedRoles[fileOf(path)] = role;
            };
            // 808s: A1 in tune, A1 30 cents flat, G1, E2 (the fifth of A).
            put("808/808 A.wav", tone(55.0 * std::pow(2.0, 2.0 / 12.0), 55.0, 0.06, 1.2, 10.0), "808");
            put("808/808 A grave.wav",
                tone(55.0 * std::pow(2.0, -0.3 / 12.0), 55.0 * std::pow(2.0, -0.3 / 12.0), 0.0, 1.2, 10.0),
                "808");
            put("808/808 G.wav", tone(49.0, 49.0, 0.0, 1.2, 10.0), "808");
            put("808/808 E.wav", tone(82.4, 82.4, 0.0, 1.2, 10.0), "808");
            // Kicks: one whose low end sits on the 808's (55 Hz), the others
            // higher, of many shapes: a role is measured against its own.
            put("Kicks/Kick Colle.wav", tone(120.0, 55.0, 0.03, 0.4, 40.0), "kick");
            for (int index = 0; index < 7; ++index)
            {
                const auto low = 85.0 + 6.0 * index;
                put("Kicks/Kick " + std::to_string(index + 1) + ".wav",
                    tone(low * (1.8 + 0.1 * index),
                         low,
                         0.02 + 0.005 * index,
                         0.25 + 0.03 * index,
                         70.0 - 4.0 * index),
                    "kick");
            }
            // Snares and claps: noise, with a body for some.
            for (int index = 0; index < 6; ++index)
            {
                auto snare =
                    noise(0.2 + 0.03 * index, 55.0 - 4.0 * index, 11U + static_cast<unsigned>(index), 1);
                const auto body = tone(
                    220.0 - 10.0 * index, 190.0 - 10.0 * index, 0.02, 0.2 + 0.03 * index, 40.0, 0.05 * index);
                for (std::size_t at = 0; at < snare.size() && at < body.size(); ++at)
                    snare[at] += body[at];
                put("Snares/Snare " + std::to_string(index + 1) + ".wav", snare, "snare");
            }
            put("Claps/Clap 01.wav", noise(0.2, 60.0, 30, 1), "clap");
            // Hats: closed short, open long.
            for (int index = 0; index < 6; ++index)
                put("Hats/HH " + std::to_string(index + 1) + ".wav",
                    noise(0.07 + 0.01 * index, 320.0 - 15.0 * index, 21U + static_cast<unsigned>(index), 1),
                    "closedHat");
            for (int index = 0; index < 4; ++index)
                put("Hats/Open Hat " + std::to_string(index + 1) + ".wav",
                    noise(0.5 + 0.1 * index, 30.0 - 3.0 * index, 41U + static_cast<unsigned>(index), 1),
                    "openHat");
            for (int index = 0; index < 4; ++index)
                put("Perc/Shaker " + std::to_string(index + 1) + ".wav",
                    noise(0.12 + 0.02 * index, 130.0 - 10.0 * index, 51U + static_cast<unsigned>(index), 1),
                    "percussion");
            // Not audio: left out, said.
            static_cast<void>(run->library.getChildFile("Snares/Casse.wav").replaceWithText("pas un son"));
            // A name that says nothing: measured, no role.
            writeWav(run->library.getChildFile("Divers/Ambiance.wav"), noise(1.0, 3.0, 61, 0));

            samples_.addFolder(run->library);
            check(true,
                  std::to_string(run->expectedRoles.size()) +
                      " samples à un rôle, un illisible, un sans nom");
        });

    add("la tonalité du projet : la mineur (direction.set)",
        [this]
        {
            domain::direction::Direction direction = state_.direction();
            direction.corrections.key = domain::generation::Key{9, domain::generation::Mode::minor};
            check(bus_.execute(std::make_unique<domain::SetDirection>(direction)).ok(),
                  "la tonalité est posée");
        });

    add(
        "la page « Kit » s'ouvre",
        [this] { press("Kit"); },
        [window] { return window() != nullptr && window()->isShowing(); },
        3000.0);

    add(
        "« Mesurer mes samples » : l'index se construit hors du fil des messages",
        [window]
        {
            if (auto* kit = window(); kit != nullptr)
                kit->measure();
        },
        settled,
        120000.0);

    add("l'index : chaque sample mesuré une fois, son rôle reconnu, rangé avec les réglages de la machine",
        [this, run]
        {
            const auto* built = kitSession_ != nullptr ? kitSession_->lastBuild() : nullptr;
            if (built == nullptr)
            {
                check(false,
                      "un index : " + (kitSession_ != nullptr ? kitSession_->status() : std::string{}));
                return;
            }
            note(kitSession_->status());
            note(std::to_string(built->measured) + " mesurés, " + std::to_string(built->reused) +
                 " repris, " + std::to_string(built->unreadable) + " illisible(s), " +
                 fixedOf(built->seconds) + " s");
            check(built->measured == run->expectedRoles.size() + 1, "tous les sons lisibles sont mesurés");
            check(built->unreadable == 1, "le fichier qui n'est pas un son est écarté");
            std::size_t right = 0;
            for (const auto& entry : built->entries)
            {
                const auto name = fileOf(entry.path);
                const auto expected = run->expectedRoles.find(name);
                const std::string got = entry.role ? std::string{domain::kit::tokenOf(*entry.role)} : "aucun";
                if (expected == run->expectedRoles.end())
                {
                    check(!entry.role.has_value(), name + " : aucun rôle (" + got + ")");
                    continue;
                }
                if (got == expected->second)
                    ++right;
                else
                    note(name + " : " + got + " au lieu de " + expected->second);
                if (entry.role == domain::kit::Role::bass808)
                    note(name + " : " + domain::kit::noteName(entry.features.pitchClass) + " " +
                         fixedOf(entry.features.cents, 0) + " cents, glissade " +
                         fixedOf(entry.features.glideSemitones, 1) + " demi-tons");
            }
            check(right == run->expectedRoles.size(),
                  std::to_string(right) + " rôles sur " + std::to_string(run->expectedRoles.size()) +
                      " reconnus");
            check(folder_.getChildFile("reglages-machine/samples-index.json").existsAsFile(),
                  "l'index est rangé avec les réglages de la vérification");
        });

    add("« Composer un kit », axes au centre : la 808 en la, un kick qui lui laisse le grave",
        [this, run, window]
        {
            auto* kit = window();
            if (kit == nullptr || kitSession_ == nullptr || kitSession_->lastBuild() == nullptr)
                return;
            kit->setAxes({});
            kit->compose();
            const auto* chosen = kitSession_->kit();
            if (chosen == nullptr)
            {
                check(false, "un kit");
                return;
            }
            for (const auto& line : kit->shown())
                note(line);
            const auto byRole = [&](domain::kit::Role role) -> const domain::kit::Pick*
            {
                for (const auto& pick : chosen->picks)
                    if (pick.role == role)
                        return &pick;
                return nullptr;
            };
            const auto featuresOf = [&](const std::string& path) -> const domain::kit::Features*
            {
                for (const auto& entry : kitSession_->lastBuild()->entries)
                    if (entry.path == path)
                        return &entry.features;
                return nullptr;
            };

            const auto* bass = byRole(domain::kit::Role::bass808);
            const auto* kick = byRole(domain::kit::Role::kick);
            check(bass != nullptr && fileOf(bass->path) == "808 A.wav", "la 808 en la, à ±15 cents");
            check(kick != nullptr && fileOf(kick->path) != "Kick Colle.wav",
                  "pas le kick posé sur le grave de la 808");
            if (bass != nullptr && kick != nullptr)
            {
                const auto* b = featuresOf(bass->path);
                const auto* k = featuresOf(kick->path);
                if (b != nullptr && k != nullptr)
                {
                    const auto correlation = domain::kit::lowCorrelation(*k, *b);
                    const auto ratio = k->lowPeakHz / b->pitchHz;
                    note("corrélation des graves " + fixedOf(correlation) + ", rapport " + fixedOf(ratio));
                    check(correlation < domain::kit::overlapCorrelation, "les graves ne se recouvrent pas");
                    check(ratio >= domain::kit::lowApart || ratio <= 1.0 / domain::kit::lowApart,
                          "un quart d'écart au moins");
                    check(std::abs(b->cents) <= domain::kit::tunedCents, "accordée à 15 cents");
                }
            }
            bool together = true;
            for (const auto& a : chosen->picks)
                for (const auto& b : chosen->picks)
                    together = together &&
                               std::abs(a.axes.bright - b.axes.bright) <= domain::kit::colourApart &&
                               std::abs(a.axes.ample - b.axes.ample) <= domain::kit::colourApart &&
                               std::abs(a.axes.dirty - b.axes.dirty) <= domain::kit::colourApart;
            check(together, "chaque élément à 0,75 des autres sur chaque axe");
            check(chosen->picks.size() >= 5, std::to_string(chosen->picks.size()) + " éléments");
            run->firstKit.clear();
            for (const auto& pick : chosen->picks)
                run->firstKit.push_back(pick.path);
        });

    add("le même choix à entrée égale",
        [this, run, window]
        {
            auto* kit = window();
            if (kit == nullptr || kitSession_ == nullptr)
                return;
            kit->compose();
            std::vector<std::string> again;
            if (const auto* chosen = kitSession_->kit(); chosen != nullptr)
                for (const auto& pick : chosen->picks)
                    again.push_back(pick.path);
            check(again == run->firstKit, "le même kit");
        });

    add(
        "« Écouter » : deux mesures du kit, mixées en mémoire, jouées par la prévisualisation",
        [window]
        {
            if (auto* kit = window(); kit != nullptr)
                kit->listen();
        },
        [this]
        {
            return kitSession_ != nullptr && samples_.auditioned() == kitSession_->previewFile() &&
                   samples_.auditionPeakDb() > -40.0f;
        },
        5000.0);

    add("« Poser le kit » : une piste par élément sur son sample, un seul groupe",
        [this, run, window]
        {
            auto* kit = window();
            if (kit == nullptr || kitSession_ == nullptr || kitSession_->kit() == nullptr)
                return;
            kitSession_->stop();
            run->projectBefore = domain::json::write(state_.toValue());
            run->depthBefore = depth();
            run->tracksBefore = state_.tracks().size();
            check(kit->pose(), "posé");
            note(kitSession_->status());
            const auto picks = kitSession_->kit()->picks.size();
            check(depth() == run->depthBefore + 1, "un seul pas d'historique");
            check(state_.tracks().size() == run->tracksBefore + picks,
                  std::to_string(picks) + " pistes de plus");
            std::size_t sampled = 0;
            for (std::size_t index = run->tracksBefore; index < state_.tracks().size(); ++index)
                sampled += state_.tracks()[index].sample.has_value() ? 1 : 0;
            check(sampled == picks, "chacune sur son sample");
        });

    add("un Ctrl+Z : le projet comme avant, à l'octet",
        [this, run]
        {
            check(bus_.undo().ok(), "défait");
            check(domain::json::write(state_.toValue()) == run->projectBefore,
                  "le même projet, octet pour octet");
        });

    add(
        "mesurer de nouveau : rien n'est mesuré deux fois",
        [window]
        {
            if (auto* kit = window(); kit != nullptr)
                kit->measure();
        },
        settled,
        60000.0);

    add("l'index relu",
        [this, run]
        {
            const auto* built = kitSession_ != nullptr ? kitSession_->lastBuild() : nullptr;
            if (built == nullptr)
                return;
            note(std::to_string(built->measured) + " mesurés, " + std::to_string(built->reused) +
                 " repris, " + fixedOf(built->seconds, 3) + " s");
            check(built->measured == 0, "aucun sample mesuré deux fois");
            check(built->reused == run->expectedRoles.size() + 1, "tous repris");
        });

    // --- what the index costs: ten thousand files

    add("dix mille samples construits",
        [this, run]
        {
            run->big = folder_.getChildFile("dix-mille");
            static_cast<void>(run->big.deleteRecursively());
            for (std::size_t index = 0; index < bigLibrary; ++index)
            {
                const auto hz = 60.0 + static_cast<double>(index % 400);
                const auto name = "Kicks/Kick " + std::to_string(index) + ".wav";
                writeWav(run->big.getChildFile(name), tone(hz * 2.0, hz, 0.03, 0.25, 50.0), 16);
            }
            samples_.removeFolder(run->library);
            samples_.addFolder(run->big);
            check(true, std::to_string(bigLibrary) + " fichiers de 0,25 s");
        });

    add(
        "l'index des dix mille, la première fois",
        [window]
        {
            if (auto* kit = window(); kit != nullptr)
                kit->measure();
        },
        settled,
        900000.0);

    add(
        "puis la seconde, rien n'ayant changé",
        [this]
        {
            if (const auto* built = kitSession_ != nullptr ? kitSession_->lastBuild() : nullptr;
                built != nullptr)
            {
                note("première fois : " + std::to_string(built->measured) + " mesurés en " +
                     fixedOf(built->seconds, 1) + " s, " +
                     fixedOf(built->seconds * 1000.0 /
                                 static_cast<double>(std::max<std::size_t>(1, built->measured)),
                             2) +
                     " ms par fichier, sur " +
                     std::to_string(std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1)) +
                     " fils ; machine non garantie au repos");
                check(built->measured == bigLibrary, "les dix mille mesurés");
            }
            if (auto* kit = dynamic_cast<ui::KitPanel*>(panel("kit")); kit != nullptr)
                kit->measure();
        },
        settled,
        300000.0);

    add("le coût, dit ; la bibliothèque de dix mille retirée",
        [this, run]
        {
            if (const auto* built = kitSession_ != nullptr ? kitSession_->lastBuild() : nullptr;
                built != nullptr)
            {
                note("seconde fois : " + std::to_string(built->reused) + " repris en " +
                     fixedOf(built->seconds, 2) + " s");
                check(built->reused == bigLibrary && built->measured == 0, "rien de mesuré deux fois");
            }
            samples_.removeFolder(run->big);
            static_cast<void>(run->big.deleteRecursively());

            const auto personal = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                      .getChildFile("DAW IA/samples-index.json");
            check(personal.existsAsFile() == run->personalIndexExisted &&
                      (!personal.existsAsFile() ||
                       personal.getLastModificationTime() == run->personalIndexBefore),
                  "l'index de la machine du fondateur n'est pas touché");
        });
}

} // namespace daw::app
