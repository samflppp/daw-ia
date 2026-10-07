#include "Verification.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/flux/Graph.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/engine/MeterTap.h"
#include "daw/engine/Rendering.h"
#include "daw/ui/panels/FluxPanel.h"
#include "daw/ui/panels/InsertSlots.h"
#include "daw/ui/panels/MixerPanel.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

// --verify-flux (S24): the effects from the mixer's strip, then the audio
// flux. The strip: each gesture of its slots makes the command it should,
// and a bypass is proved in the render — an effect bypassed sounds as if it
// were not there, and the same effect playing does not. The flux window: the
// graph of the project, its navigation, the states on the screen armed and
// no other, and the before and the after of an effect, measured live.

namespace daw::app
{
namespace
{

std::string fixed(double value, int decimals = 2)
{
    return juce::String(value, decimals).toStdString();
}

double rmsDb(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto rms = buffer.getRMSLevel(channel, 0, buffer.getNumSamples());
        sum += static_cast<double>(rms) * rms;
    }
    const auto mean = sum / std::max(1, buffer.getNumChannels());
    return mean > 0.0 ? 10.0 * std::log10(mean) : -240.0;
}

// How far apart two renders are, sample by sample, against the second: -240
// for the same samples.
double apartDb(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const auto samples = std::min(a.getNumSamples(), b.getNumSamples());
    const auto channels = std::min(a.getNumChannels(), b.getNumChannels());
    if (samples <= 0 || channels <= 0)
        return 0.0;
    juce::AudioBuffer<float> difference{channels, samples};
    for (int channel = 0; channel < channels; ++channel)
    {
        difference.copyFrom(channel, 0, a, channel, 0, samples);
        difference.addFrom(channel, 0, b, channel, 0, samples, -1.0f);
    }
    return rmsDb(difference) - rmsDb(b);
}

} // namespace

struct Verification::FluxRun
{
    domain::TrackId synth;
    domain::TrackId reverb;
    domain::TrackId second;
    domain::PluginId flowEqualiser;
    domain::PluginId flowCompressor;
    float sourceHeardDb{-100.0f};
    float sumHeardDb{-100.0f};
    domain::PluginId equaliser;
    domain::PluginId compressor;
    juce::AudioBuffer<float> playing;
    juce::AudioBuffer<float> bypassed;
    juce::AudioBuffer<float> without;
};

juce::AudioBuffer<float> Verification::renderNamed(const std::string& name)
{
    const auto file = folder_.getChildFile(name + ".wav");
    static_cast<void>(file.deleteFile());
    if (!engine::renderAsPlayed(edit_, file))
    {
        check(false, "le rendu « " + name + " » a abouti");
        return {};
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        check(false, "le rendu « " + name + " » n'est pas vide");
        return {};
    }
    juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples)};
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
}

std::string Verification::lastCommandType() const
{
    const auto journal = bus_.journal();
    if (journal.empty())
        return {};
    const auto type = journal.back().stringAt("type");
    return type ? type.value() : std::string{};
}

void Verification::buildFlux()
{
    auto run = std::make_shared<FluxRun>();

    add("une piste qui joue un enregistrement : quatre secondes de bruit, toujours les mêmes",
        [this, run]
        {
            // A recording, not an instrument: 4OSC starts its oscillators
            // at another phase at each render, and two renders of the same
            // chain would differ sample by sample.
            const auto file = folder_.getChildFile("flux-source.wav");
            {
                constexpr double rate = 48000.0;
                juce::AudioBuffer<float> noise{2, static_cast<int>(4.0 * rate)};
                juce::Random random{2024};
                for (int index = 0; index < noise.getNumSamples(); ++index)
                {
                    const auto value = 0.25f * (2.0f * random.nextFloat() - 1.0f);
                    noise.setSample(0, index, value);
                    noise.setSample(1, index, value);
                }
                static_cast<void>(file.deleteFile());
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
                juce::WavAudioFormat wav;
                auto writer = wav.createWriterFor(stream,
                                                  juce::AudioFormatWriterOptions{}
                                                      .withSampleRate(rate)
                                                      .withNumChannels(2)
                                                      .withBitsPerSample(24));
                check(writer != nullptr &&
                          writer->writeFromAudioSampleBuffer(noise, 0, noise.getNumSamples()),
                      "la source est écrite");
            }
            const auto sample = samples_.import(file);
            check(sample.ok(), "la source entre dans le projet");
            if (!sample.ok())
                return;
            run->synth = domain::TrackId::generate();
            check(bus_.execute(std::make_unique<domain::AddTrack>(run->synth, "Source")).ok(), "la piste");
            check(bus_.execute(std::make_unique<domain::PlaceAudio>(
                                   domain::AudioClipId::generate(), run->synth, sample.value(), 0.0))
                      .ok(),
                  "posée au début");
            check(bus_.execute(std::make_unique<domain::TransportSetMode>(domain::PlayMode::song,
                                                                          domain::PatternId{}))
                      .ok(),
                  "SONG");
        });

    add(
        "F10 ouvre le mixer, la tranche de Synthé n'a aucun effet",
        [this] { key(juce::KeyPress{juce::KeyPress::F10Key}); },
        [this, run]
        {
            const auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
            return mixer != nullptr && mixer->insertsOf(run->synth) != nullptr;
        },
        3000.0);

    const auto slots = [this, run]() -> ui::InsertSlots*
    {
        const auto* mixer = dynamic_cast<ui::MixerPanel*>(panel("mixer"));
        return mixer != nullptr ? mixer->insertsOf(run->synth) : nullptr;
    };

    add("« ＋ effet », l'égaliseur de DAW IA : plugin.insert, en bout de chaîne",
        [this, run, slots]
        {
            auto* inserts = slots();
            if (inserts == nullptr)
                return;
            check(inserts->shown() == std::vector<std::string>{"＋ effet"}, "la tranche est vide");
            inserts->insert(domain::PluginRef{std::string{domain::PluginRef::internalFormat},
                                              std::string{domain::internal::equaliser},
                                              "Égaliseur"});
            check(lastCommandType() == "plugin.insert", "la commande : " + lastCommandType());
            const auto* strip = state_.findTrack(run->synth);
            check(strip != nullptr && strip->plugins.size() == 1, "un effet dans la chaîne");
            if (strip != nullptr && !strip->plugins.empty())
                run->equaliser = strip->plugins.front().id;
        });

    add("puis le compresseur, et la tranche les montre dans l'ordre",
        [this, run, slots]
        {
            auto* inserts = slots();
            if (inserts == nullptr)
                return;
            inserts->insert(domain::PluginRef{std::string{domain::PluginRef::internalFormat},
                                              std::string{domain::internal::compressor},
                                              "Compresseur"});
            check(lastCommandType() == "plugin.insert", "la commande : " + lastCommandType());
            const auto* strip = state_.findTrack(run->synth);
            if (strip != nullptr && strip->plugins.size() == 2)
                run->compressor = strip->plugins.back().id;
            inserts->refresh();
            const auto shown = inserts->shown();
            for (const auto& line : shown)
                note(line);
            check(shown.size() == 3 && shown[0].rfind("● Égaliseur", 0) == 0 &&
                      shown[1].rfind("● Compresseur", 0) == 0,
                  "égaliseur puis compresseur, tous deux allumés");
        });

    add("glisser le compresseur en tête : plugin.move, le même plugin à sa nouvelle place",
        [this, run, slots]
        {
            auto* inserts = slots();
            if (inserts == nullptr)
                return;
            const auto before = state_.findPlugin(run->compressor) != nullptr
                                    ? *state_.findPlugin(run->compressor)
                                    : domain::PluginInstance{};
            inserts->move(1, 0);
            check(lastCommandType() == "plugin.move", "la commande : " + lastCommandType());
            const auto* strip = state_.findTrack(run->synth);
            check(strip != nullptr && strip->plugins.size() == 2 &&
                      strip->plugins.front().id == run->compressor && strip->plugins.front() == before,
                  "le compresseur en tête, le même, réglages compris");
            inserts->refresh();
            check(inserts->shown()[0].rfind("● Compresseur", 0) == 0, "la tranche le montre en tête");
        });

    add("le coupe-bas de l'égaliseur à 1 kHz : la tranche le dit",
        [this, run, slots]
        {
            check(bus_.execute(std::make_unique<domain::SetPluginParameter>(
                                   run->equaliser, std::string{domain::internal::highPassFrequency}, 1000.0))
                      .ok(),
                  "le réglage");
            auto* inserts = slots();
            if (inserts == nullptr)
                return;
            inserts->refresh();
            check(inserts->shown()[1] == "● Égaliseur · coupe-bas 1000 Hz",
                  "« " + inserts->shown()[1] + " »");
        });

    add("le point du compresseur : plugin.set_bypassed, et la tranche le montre éteint",
        [this, slots]
        {
            auto* inserts = slots();
            if (inserts == nullptr)
                return;
            inserts->toggleBypass(0);
            check(lastCommandType() == "plugin.set_bypassed", "la commande : " + lastCommandType());
            inserts->refresh();
            check(inserts->shown()[0].rfind("○ Compresseur", 0) == 0, "« " + inserts->shown()[0] + " »");
        });

    add("rendu, l'égaliseur jouant", [this, run] { run->playing = renderNamed("flux-egaliseur-joue"); });

    add("l'égaliseur contourné : rendu",
        [this, run, slots]
        {
            auto* inserts = slots();
            if (inserts == nullptr)
                return;
            inserts->toggleBypass(1);
            check(lastCommandType() == "plugin.set_bypassed", "la commande : " + lastCommandType());
            run->bypassed = renderNamed("flux-egaliseur-contourne");
        });

    add("les deux retirés par le menu de la tranche : plugin.remove, rendu sans effet",
        [this, run, slots]
        {
            auto* inserts = slots();
            if (inserts == nullptr)
                return;
            inserts->remove(0);
            check(lastCommandType() == "plugin.remove", "la commande : " + lastCommandType());
            inserts->refresh();
            inserts->remove(0);
            const auto* strip = state_.findTrack(run->synth);
            check(strip != nullptr && strip->plugins.empty(), "la chaîne est vide");
            run->without = renderNamed("flux-sans-effet");
        });

    add("au rendu : contourné, l'effet ne fait rien ; jouant, il fait",
        [this, run]
        {
            const auto bypassed = apartDb(run->bypassed, run->without);
            const auto playing = apartDb(run->playing, run->without);
            note("contourné contre sans effet : " + fixed(bypassed) +
                 " dB ; jouant contre sans effet : " + fixed(playing) + " dB ; niveaux " +
                 fixed(rmsDb(run->playing)) + " / " + fixed(rmsDb(run->without)) + " dBFS RMS");
            check(rmsDb(run->without) > -40.0, "le rendu sans effet s'entend");
            check(bypassed < -90.0, "contourné, le rendu est celui sans effet, échantillon par échantillon");
            check(playing > -20.0, "jouant, le coupe-bas change le rendu");
        });

    addFluxWindow(run);
}

void Verification::addFluxWindow(const std::shared_ptr<FluxRun>& run)
{
    const auto flux = [this]() -> ui::FluxPanel* { return dynamic_cast<ui::FluxPanel*>(panel("flux")); };
    // Only the DAW's own effects here: no plugin is an instrument.
    const auto isInstrument = [](const domain::PluginRef&) { return false; };

    add("un bus « Réverb », un envoi de la source à -12 dB, l'égaliseur remis, coupe-bas à 1 kHz",
        [this, run]
        {
            run->reverb = domain::TrackId::generate();
            check(bus_.execute(std::make_unique<domain::AddBus>(run->reverb, "Réverb")).ok(), "le bus");
            check(bus_.execute(std::make_unique<domain::SetTrackSend>(run->synth, run->reverb, -12.0)).ok(),
                  "l'envoi");
            domain::PluginInstance equaliser{};
            equaliser.id = domain::PluginId::generate();
            equaliser.ref = domain::PluginRef{std::string{domain::PluginRef::internalFormat},
                                              std::string{domain::internal::equaliser},
                                              "Égaliseur"};
            run->flowEqualiser = equaliser.id;
            check(bus_.execute(std::make_unique<domain::InsertPlugin>(run->synth, equaliser, 0)).ok(),
                  "l'égaliseur");
            check(bus_.execute(std::make_unique<domain::SetPluginParameter>(
                                   equaliser.id, std::string{domain::internal::highPassFrequency}, 1000.0))
                      .ok(),
                  "le coupe-bas");
        });

    add(
        "F3 ouvre le flux audio",
        [this] { key(juce::KeyPress{juce::KeyPress::F3Key}); },
        [flux] { return flux() != nullptr && flux()->isShowing(); },
        3000.0);

    add("le graphe est celui du projet : la source, l'égaliseur, le fader, l'envoi, le bus, le master",
        [this, run, flux, isInstrument]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            const auto expected = domain::flux::graphOf(state_, isInstrument);
            const auto& graph = window->graph();
            note(std::to_string(graph.nodes.size()) + " nœuds, " + std::to_string(graph.links.size()) +
                 " liens");
            check(graph.nodes.size() == expected.nodes.size() && graph.links.size() == expected.links.size(),
                  "autant de nœuds et de liens que le graphe du projet");
            const auto afterEq = domain::flux::afterEffect(run->synth, run->flowEqualiser);
            check(graph.link(domain::flux::sourceOf(run->synth),
                             domain::flux::effectNode(run->flowEqualiser)) != nullptr,
                  "la source entre dans l'égaliseur");
            check(graph.link(afterEq, domain::flux::faderNode(run->synth)) != nullptr,
                  "l'égaliseur va au fader");
            const auto* send =
                graph.link(domain::flux::afterFader(run->synth), "s:" + run->reverb.toString() + ":sum");
            check(send != nullptr && send->kind == domain::flux::LinkKind::send && send->levelDb == -12.0,
                  "l'envoi à -12 dB vers « Réverb »");
        });

    add("F cadre tout le graphe dans la fenêtre",
        [this, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            check(window->keyPressed(juce::KeyPress{'f'}), "F est pris par la fenêtre");
            const auto inside = window->getLocalBounds().toFloat();
            bool all = true;
            for (const auto& node : window->graph().nodes)
                all = all && inside.contains(window->boundsOf(node.id));
            note("zoom " + std::to_string(std::lround(window->zoom() * 100.0)) + " %");
            check(all, "chaque nœud est dans la fenêtre");
        });

    add("Ctrl+molette zoome autour de la souris : le nœud sous elle ne bouge pas",
        [this, run, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            const auto node = domain::flux::effectNode(run->flowEqualiser);
            const auto before = window->boundsOf(node);
            const auto zoom = window->zoom();
            wheel(*window, before.getCentre().toInt(), 0.5f, false, true);
            const auto after = window->boundsOf(node);
            note("zoom " + std::to_string(std::lround(zoom * 100.0)) + " % puis " +
                 std::to_string(std::lround(window->zoom() * 100.0)) + " %");
            check(window->zoom() > zoom, "le zoom grandit");
            check(after.getCentre().getDistanceFrom(before.getCentre()) < 2.0f,
                  "l'égaliseur reste sous la souris");
            check(window->keyPressed(juce::KeyPress{'f'}), "F recadre");
        });

    add(
        "la lecture en boucle : la source se voit et s'entend dans le flux",
        [this]
        {
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetLoop>(true, 0.0, 8.0)));
            static_cast<void>(bus_.execute(std::make_unique<domain::TransportSetPosition>(0.0)));
            key(juce::KeyPress{juce::KeyPress::spaceKey});
        },
        [this, run, flux]
        {
            auto* window = flux();
            return clock_.isPlaying() && window != nullptr &&
                   window->levelAt(domain::flux::sourceOf(run->synth)) > -30.0;
        },
        5000.0);

    add("ce qui est armé : les états de la fenêtre, et eux seuls",
        [this, run, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            std::size_t states = 0;
            for (const auto& node : window->graph().nodes)
                states += window->placeOf(node).has_value() ? 1 : 0;
            note(std::to_string(window->armed().size()) + " places armées sur " + std::to_string(states) +
                 " états");
            check(window->armed().size() == states, "tout le graphe est à l'écran, chaque état est armé");
            const auto level = window->levelAt(domain::flux::sourceOf(run->synth));
            check(level > -30.0, "la source : " + fixed(level, 1) + " dBFS");
            check(!window->shownAt(domain::flux::sourceOf(run->synth)).empty(), "sa forme d'onde est lue");
        });

    const auto bandAt = [](double hz)
    {
        const auto bands = static_cast<double>(ui::Tokens::builtIn().integer("metric.flux.spectrumBands"));
        return static_cast<std::size_t>(std::log(hz / 30.0) / std::log(16000.0 / 30.0) * bands);
    };

    add(
        "un clic sur l'égaliseur : son avant et son après",
        [this, run, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            click(*window,
                  window->boundsOf(domain::flux::effectNode(run->flowEqualiser)).getCentre().toInt());
            check(window->selected() == domain::flux::effectNode(run->flowEqualiser),
                  "l'égaliseur est choisi");
        },
        [flux, bandAt]
        {
            auto* window = flux();
            return window != nullptr && !window->spectrumBefore().empty() &&
                   window->spectrumBefore()[bandAt(100.0)] > -80.0;
        },
        3000.0);

    add("au spectre : 100 Hz coupés de plus de 20 dB, 5 kHz pareils à 3 dB près",
        [this, flux, bandAt]
        {
            auto* window = flux();
            if (window == nullptr || window->spectrumBefore().empty() || window->spectrumAfter().empty())
            {
                check(false, "les deux spectres sont là");
                return;
            }
            const auto low = bandAt(100.0);
            const auto high = bandAt(5000.0);
            const auto& before = window->spectrumBefore();
            const auto& after = window->spectrumAfter();
            note("100 Hz : " + fixed(before[low], 1) + " puis " + fixed(after[low], 1) +
                 " dB ; 5 kHz : " + fixed(before[high], 1) + " puis " + fixed(after[high], 1) + " dB");
            check(before[low] - after[low] > 20.0, "le grave est coupé");
            check(std::abs(before[high] - after[high]) < 3.0, "l'aigu passe");
        });

    add(
        "l'égaliseur contourné : l'avant et l'après se rejoignent",
        [this, run]
        {
            check(bus_.execute(std::make_unique<domain::SetPluginBypassed>(run->flowEqualiser, true)).ok(),
                  "contourné");
            stepStartedMs_ = juce::Time::getMillisecondCounterHiRes();
        },
        [this] { return juce::Time::getMillisecondCounterHiRes() - stepStartedMs_ > 1500.0; },
        3000.0);

    add("au spectre, contourné : chaque bande audible à 1 dB près",
        [this, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            const auto& before = window->spectrumBefore();
            const auto& after = window->spectrumAfter();
            double widest = 0.0;
            for (std::size_t band = 0; band < before.size() && band < after.size(); ++band)
                if (before[band] > -70.0)
                    widest = std::max(widest, std::abs(before[band] - after[band]));
            note("le plus grand écart : " + fixed(widest, 2) + " dB");
            check(!before.empty() && widest < 1.0, "l'après est l'avant");
        });

    add("la vue déplacée loin du graphe : rien d'armé que l'avant et l'après de l'égaliseur",
        [this, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            for (int notch = 0; notch < 40; ++notch)
                wheel(*window, window->getLocalBounds().getCentre(), -1.0f);
            window->frame();
            note(std::to_string(window->armed().size()) + " places armées");
            check(window->armed().size() == 2, "deux : l'avant et l'après de l'effet regardé");
            static_cast<void>(window->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}));
            window->frame();
            check(window->armed().empty(), "Échap : aucune");
            check(window->keyPressed(juce::KeyPress{'f'}), "F ramène le graphe");
        });

    // --- the gestures of the graph, through the commands that exist

    const auto pluginsOf = [this](domain::TrackId strip)
    {
        std::vector<domain::PluginId> ids;
        if (const auto* track = state_.findStrip(strip); track != nullptr)
            for (const auto& plugin : track->plugins)
                ids.push_back(plugin.id);
        return ids;
    };
    // The window hears of a change on the next message: a gesture waits for
    // its graph to have it.
    const auto shows = [flux](const std::string& node)
    { return flux() != nullptr && flux()->graph().find(node) != nullptr; };
    // A point on a link, halfway between the two nodes it joins, on the row
    // it leaves from.
    const auto onLink = [flux](const std::string& from, const std::string& to)
    {
        auto* window = flux();
        const auto a = window->boundsOf(from);
        const auto b = window->boundsOf(to);
        return juce::Point<float>{(a.getRight() + b.getX()) / 2.0f, a.getCentreY()}.toInt();
    };

    add(
        "un clic droit sur le lien de la source à l'égaliseur : le compresseur inséré là (plugin.insert)",
        [this, run, flux, pluginsOf]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            check(window->insertOn(domain::flux::sourceOf(run->synth),
                                   domain::flux::effectNode(run->flowEqualiser),
                                   domain::PluginRef{std::string{domain::PluginRef::internalFormat},
                                                     std::string{domain::internal::compressor},
                                                     "Compresseur"}),
                  "le geste aboutit");
            check(lastCommandType() == "plugin.insert", "la commande : " + lastCommandType());
            const auto ids = pluginsOf(run->synth);
            check(ids.size() == 2 && ids.back() == run->flowEqualiser,
                  "le compresseur en tête, l'égaliseur après");
            if (!ids.empty())
                run->flowCompressor = ids.front();
        },
        [run, shows] { return shows(domain::flux::effectNode(run->flowCompressor)); },
        3000.0);

    add(
        "l'égaliseur glissé sur le lien de la source au compresseur : plugin.move, en tête",
        [this, run, flux, onLink, pluginsOf]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            const auto from =
                window->boundsOf(domain::flux::effectNode(run->flowEqualiser)).getCentre().toInt();
            const auto to =
                onLink(domain::flux::sourceOf(run->synth), domain::flux::effectNode(run->flowCompressor));
            drag(*window, from, to);
            check(lastCommandType() == "plugin.move", "la commande : " + lastCommandType());
            const auto ids = pluginsOf(run->synth);
            check(ids.size() == 2 && ids.front() == run->flowEqualiser, "l'égaliseur en tête");
        },
        [run, flux]
        {
            return flux() != nullptr &&
                   flux()->graph().link(domain::flux::sourceOf(run->synth),
                                        domain::flux::effectNode(run->flowEqualiser)) != nullptr;
        },
        3000.0);

    add(
        "l'égaliseur glissé dans la chaîne de « Réverb » : retiré et posé, un seul Ctrl+Z",
        [this, run, flux, onLink, pluginsOf]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            const auto depthBefore = depth();
            const auto settings = state_.findPlugin(run->flowEqualiser) != nullptr
                                      ? *state_.findPlugin(run->flowEqualiser)
                                      : domain::PluginInstance{};
            const auto from =
                window->boundsOf(domain::flux::effectNode(run->flowEqualiser)).getCentre().toInt();
            const auto sum = "s:" + run->reverb.toString() + ":sum";
            drag(*window, from, onLink(sum, domain::flux::faderNode(run->reverb)));
            const auto onBus = pluginsOf(run->reverb);
            check(onBus.size() == 1 && onBus.front() == run->flowEqualiser, "l'égaliseur est sur « Réverb »");
            check(pluginsOf(run->synth).size() == 1, "la source n'a plus que le compresseur");
            check(state_.findPlugin(run->flowEqualiser) != nullptr &&
                      *state_.findPlugin(run->flowEqualiser) == settings,
                  "le même effet, réglages compris");
            check(depth() == depthBefore + 1, "un seul pas d'historique");
        },
        [run, flux]
        {
            return flux() != nullptr &&
                   flux()->graph().link("s:" + run->reverb.toString() + ":sum",
                                        domain::flux::effectNode(run->flowEqualiser)) != nullptr;
        },
        3000.0);

    add(
        "un clic droit dans le vide : « Nouveau bus » (bus.add)",
        [this, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            const auto buses = state_.buses().size();
            check(window->addBus(), "le geste aboutit");
            check(lastCommandType() == "bus.add", "la commande : " + lastCommandType());
            check(state_.buses().size() == buses + 1, "un bus de plus");
        },
        [this, flux]
        {
            return flux() != nullptr && !state_.buses().empty() &&
                   flux()->graph().find("s:" + state_.buses().back().id.toString() + ":sum") != nullptr;
        },
        3000.0);

    add(
        "un trait tiré de la sortie de la source au nouveau bus : track.set_send, -12 dB",
        [this, run, flux]
        {
            auto* window = flux();
            if (window == nullptr || state_.buses().empty())
                return;
            run->second = state_.buses().back().id;
            window->frameAll();
            const auto from = window->boundsOf(domain::flux::afterFader(run->synth)).getCentre().toInt();
            const auto to = window->boundsOf("s:" + run->second.toString() + ":sum").getCentre().toInt();
            drag(*window, from, to);
            check(lastCommandType() == "track.set_send", "la commande : " + lastCommandType());
            const auto* track = state_.findTrack(run->synth);
            const auto* send = track != nullptr ? track->findSend(run->second) : nullptr;
            check(send != nullptr && send->levelDb == -12.0, "l'envoi à -12 dB");
            check(track != nullptr && track->sends.size() == 2, "l'envoi vers « Réverb » reste");
        },
        [run, flux]
        {
            return flux() != nullptr &&
                   flux()->graph().link(domain::flux::afterFader(run->synth),
                                        "s:" + run->second.toString() + ":sum") != nullptr;
        },
        3000.0);

    add(
        "le bout de la sortie de la source glissé sur « Réverb » : track.set_output",
        [this, run, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            // On the source's own row, just before its output bends into the
            // master: the end of that output and of no other.
            const auto master = "s:" + domain::ProjectState::masterTrackId().toString() + ":sum";
            const auto& tokens = ui::Tokens::builtIn();
            const auto gap = static_cast<float>(tokens.integer("metric.flux.columnWidth") -
                                                tokens.integer("metric.flux.stateWidth")) *
                             window->zoom();
            const auto row = window->boundsOf(domain::flux::afterFader(run->synth)).getCentreY();
            const auto from = juce::Point<float>{window->boundsOf(master).getX() - gap - 6.0f, row}.toInt();
            const auto to = window->boundsOf("s:" + run->reverb.toString() + ":sum").getCentre().toInt();
            drag(*window, from, to);
            check(lastCommandType() == "track.set_output", "la commande : " + lastCommandType());
            const auto* track = state_.findTrack(run->synth);
            check(track != nullptr && track->output == run->reverb, "la source sort dans « Réverb »");
        },
        [run, flux]
        {
            return flux() != nullptr &&
                   flux()->graph().link(domain::flux::afterFader(run->synth),
                                        "s:" + run->reverb.toString() + ":sum") != nullptr &&
                   flux()->graph()
                           .link(domain::flux::afterFader(run->synth), "s:" + run->reverb.toString() + ":sum")
                           ->kind == domain::flux::LinkKind::output;
        },
        3000.0);

    add("Ctrl+Z : la source sort de nouveau au master",
        [this, run]
        {
            check(bus_.undo().ok(), "défait");
            const auto* track = state_.findTrack(run->synth);
            check(track != nullptr && track->output == domain::TrackId{}, "au master");
        });

    add(
        "la pastille du compresseur : plugin.set_bypassed",
        [this, run, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            click(*window, window->dotOf(domain::flux::effectNode(run->flowCompressor)).getCentre().toInt());
            check(lastCommandType() == "plugin.set_bypassed", "la commande : " + lastCommandType());
            const auto* plugin = state_.findPlugin(run->flowCompressor);
            check(plugin != nullptr && plugin->bypassed, "contourné");
        },
        [run, flux]
        {
            const auto* node = flux() != nullptr
                                   ? flux()->graph().find(domain::flux::effectNode(run->flowCompressor))
                                   : nullptr;
            return node != nullptr && node->bypassed;
        },
        3000.0);

    add(
        "le compresseur choisi, Suppr : plugin.remove",
        [this, run, flux, pluginsOf]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            click(*window,
                  window->boundsOf(domain::flux::effectNode(run->flowCompressor)).getCentre().toInt());
            check(window->keyPressed(juce::KeyPress{juce::KeyPress::deleteKey}), "Suppr est pris");
            check(lastCommandType() == "plugin.remove", "la commande : " + lastCommandType());
            check(pluginsOf(run->synth).empty(), "la source n'a plus d'effet");
        },
        [run, flux]
        {
            return flux() != nullptr &&
                   flux()->graph().find(domain::flux::effectNode(run->flowCompressor)) == nullptr;
        },
        3000.0);

    // --- listening alone at one place: the master's meter hears it

    const auto master = engine::MeterTapPlugin::masterStrip.toStdString();
    const auto settle = [this] { return juce::Time::getMillisecondCounterHiRes() - stepStartedMs_ > 800.0; };

    // After the source's fader, where its sends leave: the fader, centred,
    // takes 3 dB (the pan law), the send 12 more.
    add(
        "un clic sur la sortie de la source, après son fader : elle s'écoute seule",
        [this, run, flux]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            click(*window, window->boundsOf(domain::flux::afterFader(run->synth)).getCentre().toInt());
            check(window->listened() == domain::flux::afterFader(run->synth), "la sortie est écoutée");
            stepStartedMs_ = juce::Time::getMillisecondCounterHiRes();
        },
        settle,
        3000.0);

    add(
        "un clic sur la somme de « Réverb », qui reçoit la source à -12 dB",
        [this, run, flux, master]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            run->sourceHeardDb = levelOf(master).peakDb;
            click(*window, window->boundsOf("s:" + run->reverb.toString() + ":sum").getCentre().toInt());
            check(window->listened() == "s:" + run->reverb.toString() + ":sum", "la somme est écoutée");
            stepStartedMs_ = juce::Time::getMillisecondCounterHiRes();
        },
        settle,
        3000.0);

    add(
        "au master : la somme 12 dB sous la sortie de la source ; Échap rend le morceau",
        [this, run, flux, master]
        {
            auto* window = flux();
            if (window == nullptr)
                return;
            run->sumHeardDb = levelOf(master).peakDb;
            note("master : la sortie de la source seule " + fixed(run->sourceHeardDb, 1) +
                 " dBFS, la somme seule " + fixed(run->sumHeardDb, 1) + " dBFS");
            const auto drop = run->sourceHeardDb - run->sumHeardDb;
            check(drop > 9.0 && drop < 15.0, "12 dB plus bas, à 3 dB près : " + fixed(drop, 1) + " dB");
            check(window->keyPressed(juce::KeyPress{juce::KeyPress::escapeKey}), "Échap est pris");
            check(window->listened().empty(), "plus rien n'est écouté seul");
            stepStartedMs_ = juce::Time::getMillisecondCounterHiRes();
        },
        settle,
        3000.0);

    add("le morceau : au moins la sortie de la source, plus ses envois",
        [this, run, master]
        {
            const auto song = levelOf(master).peakDb;
            note("master : le morceau " + fixed(song, 1) + " dBFS");
            check(song > run->sourceHeardDb - 1.0,
                  "le morceau n'est pas plus bas que la sortie de la source seule");
        });

    add("la source écoutée seule de nouveau, avant de fermer la fenêtre",
        [this, run, flux]
        {
            if (auto* window = flux(); window != nullptr)
            {
                click(*window, window->boundsOf(domain::flux::sourceOf(run->synth)).getCentre().toInt());
                check(window->listened() == domain::flux::sourceOf(run->synth), "la source est écoutée");
            }
        });

    add(
        "F3 referme le flux",
        [this] { key(juce::KeyPress{juce::KeyPress::F3Key}); },
        [flux] { return flux() == nullptr || !flux()->isShowing(); },
        3000.0);

    add("fermé, il n'arme rien ; la lecture arrêtée",
        [this, flux]
        {
            if (auto* window = flux(); window != nullptr)
            {
                check(window->armed().empty(), "la fenêtre cachée n'arme rien");
                check(window->listened().empty(), "ni n'écoute rien seul : le morceau revient");
            }
            if (clock_.isPlaying())
                key(juce::KeyPress{juce::KeyPress::spaceKey});
        });
}

} // namespace daw::app
