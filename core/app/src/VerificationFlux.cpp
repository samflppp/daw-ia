#include "Verification.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/engine/Rendering.h"
#include "daw/ui/panels/InsertSlots.h"
#include "daw/ui/panels/MixerPanel.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

// --verify-flux (S24): the effects from the mixer's strip, then the audio
// flux. Here the strip: each gesture of its slots makes the command it
// should, and a bypass is proved in the render — an effect bypassed sounds
// as if it were not there, and the same effect playing does not.

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
}

} // namespace daw::app
