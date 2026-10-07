#include "BusSession.h"

#include "daw/ui/Tokens.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace daw::app
{
namespace
{

constexpr int progressPollMs = 100;
constexpr double probeSeconds = 2.0;

std::string french(double value, int decimals = 1)
{
    std::array<char, 32> text{};
    std::snprintf(text.data(), text.size(), "%.*f", decimals, value);
    std::string said{text.data()};
    std::replace(said.begin(), said.end(), '.', ',');
    return said;
}

juce::AudioBuffer<float> readAll(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};
    juce::AudioBuffer<float> buffer{static_cast<int>(reader->numChannels),
                                    static_cast<int>(reader->lengthInSamples)};
    reader->read(&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer;
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

// The after against the before, sample by sample, in dB under the before:
// -240 for the same samples.
double apartDb(const juce::AudioBuffer<float>& after, const juce::AudioBuffer<float>& before)
{
    const auto samples = std::min(after.getNumSamples(), before.getNumSamples());
    const auto channels = std::min(after.getNumChannels(), before.getNumChannels());
    if (samples <= 0 || channels <= 0)
        return 0.0;
    juce::AudioBuffer<float> difference{channels, samples};
    for (int channel = 0; channel < channels; ++channel)
    {
        difference.copyFrom(channel, 0, after, channel, 0, samples);
        difference.addFrom(channel, 0, before, channel, 0, samples, -1.0f);
    }
    return rmsDb(difference) - rmsDb(before);
}

// The part of `reference` in `heard`, at zero delay: <heard, reference> over
// <reference, reference>, in dB.
std::optional<double> shareDb(const juce::AudioBuffer<float>& heard,
                              const juce::AudioBuffer<float>& reference)
{
    const auto samples = std::min(heard.getNumSamples(), reference.getNumSamples());
    double cross = 0.0;
    double energy = 0.0;
    for (int channel = 0; channel < std::min(heard.getNumChannels(), reference.getNumChannels()); ++channel)
        for (int index = 0; index < samples; ++index)
        {
            cross +=
                static_cast<double>(heard.getSample(channel, index)) * reference.getSample(channel, index);
            energy += static_cast<double>(reference.getSample(channel, index)) *
                      reference.getSample(channel, index);
        }
    if (energy <= 0.0)
        return std::nullopt;
    const auto gain = std::abs(cross / energy);
    return gain > 0.0 ? 20.0 * std::log10(gain) : -240.0;
}

// A known noise, as a sample of the project's store.
std::optional<domain::SampleRef> probeNoise(engine::ContentStore& store)
{
    constexpr double rate = 48000.0;
    juce::AudioBuffer<float> buffer{1, static_cast<int>(probeSeconds * rate)};
    juce::Random random{2024};
    for (int index = 0; index < buffer.getNumSamples(); ++index)
        buffer.setSample(0, index, 0.25f * (2.0f * random.nextFloat() - 1.0f));
    juce::MemoryBlock bytes;
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(bytes, false);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor(
            stream,
            juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(24));
        if (writer == nullptr || !writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
            return std::nullopt;
    }
    const auto blob = store.put(bytes.getData(), bytes.getSize());
    if (!blob.ok())
        return std::nullopt;
    domain::SampleRef sample;
    sample.blob = blob.value();
    sample.name = "sonde.wav";
    sample.format = "wav";
    sample.seconds = probeSeconds;
    return sample;
}

} // namespace

BusSession::BusSession(Wiring wiring)
    : wiring_{std::move(wiring)}
{
    if (wiring_.device != nullptr)
        comparison_ = std::make_unique<MixComparison>(*wiring_.device);
}

BusSession::~BusSession()
{
    alive_->store(false);
    cancelled_.store(true);
    join();
    stopTimer();
    clearTry();
}

void BusSession::join()
{
    if (worker_.joinable())
        worker_.join();
}

void BusSession::setStage(Stage stage, std::string status)
{
    stage_ = stage;
    status_ = std::move(status);
    sendChangeMessage();
}

void BusSession::timerCallback()
{
    sendChangeMessage();
}

std::string BusSession::categoryOf(const domain::PluginRef& ref) const
{
    if (wiring_.catalogue == nullptr)
        return {};
    const auto found = wiring_.catalogue->find(ref);
    if (!found)
        return {};
    if (found->isInstrument)
        return "instrument";
    const auto category = found->category.toLowerCase();
    if (category.contains("reverb"))
        return "reverb";
    if (category.contains("delay") || category.contains("echo"))
        return "delay";
    return category.toStdString();
}

void BusSession::propose()
{
    clearTry();
    proposals_ = domain::buses::propose(wiring_.state,
                                        [this](const domain::PluginRef& ref) { return categoryOf(ref); });
    setStage(Stage::idle,
             proposals_.empty() ? std::string{"Aucun effet partagé à proposer."}
                                : std::to_string(proposals_.size()) + " proposition" +
                                      (proposals_.size() > 1 ? "s" : "") + " de bus.");
}

void BusSession::clearTry()
{
    if (comparison_ != nullptr)
        comparison_->unload();
    tried_.reset();
    result_.reset();
    proposedState_.reset();
    proposedFlux_.clear();
    before_.reset();
    after_.reset();
    probeWith_.reset();
    probeWithout_.reset();
    static_cast<void>(beforeFile_.deleteFile());
    static_cast<void>(afterFile_.deleteFile());
    beforeFile_ = afterFile_ = juce::File{};
}

void BusSession::tryOut(std::size_t proposal)
{
    if (stage_ == Stage::trying || proposal >= proposals_.size())
        return;
    join();
    clearTry();
    const auto& shared = proposals_[proposal];

    // The proposal on a copy of the project: the commands it would write.
    proposedBus_ = domain::TrackId::generate();
    proposedPlugin_ = domain::PluginId::generate();
    auto proposed = std::make_shared<domain::ProjectState>(wiring_.state);
    const auto name =
        shared.way == domain::buses::Way::send ? std::string{"Bus d'envoi"} : std::string{"Bus de groupe"};
    for (const auto& command : domain::buses::compile(shared, proposedBus_, name, proposedPlugin_))
        static_cast<void>(command->apply(*proposed));

    auto* store = wiring_.store ? wiring_.store() : nullptr;
    before_ = engine::MixRender::prepare(wiring_.edit, wiring_.state, nullptr, wiring_.catalogue, store);
    after_ =
        engine::MixRender::prepare(wiring_.edit, wiring_.state, proposed.get(), wiring_.catalogue, store);
    if (before_ == nullptr || after_ == nullptr)
    {
        clearTry();
        setStage(Stage::failed, "La proposition n'a pas pu être essayée.");
        return;
    }
    double from = 0.0;
    if (wiring_.clock != nullptr)
        from = wiring_.edit.tempoSequence
                   .toTime(tracktion::BeatPosition::fromBeats(wiring_.clock->positionBeats()))
                   .inSeconds();
    after_->captureFlux(from, ui::Tokens::builtIn().integer("metric.flux.waveMs") / 1000.0);

    // A send's effect, alone on a known noise, with and without it.
    if (shared.way == domain::buses::Way::send && store != nullptr)
        if (const auto noise = probeNoise(*store); noise)
        {
            domain::ProjectState without;
            domain::Track probe{};
            probe.id = domain::TrackId::generate();
            probe.name = "sonde";
            static_cast<void>(without.addTrack(probe));
            domain::AudioClip clip{};
            clip.id = domain::AudioClipId::generate();
            clip.trackId = probe.id;
            clip.sample = *noise;
            static_cast<void>(without.addAudioClip(clip));
            auto with = without;
            auto instance = shared.plugin;
            instance.id = domain::PluginId::generate();
            static_cast<void>(with.insertPlugin(probe.id, instance, 0));
            probeWithout_ =
                engine::MixRender::prepare(wiring_.edit, without, &without, wiring_.catalogue, store);
            probeWith_ = engine::MixRender::prepare(wiring_.edit, without, &with, wiring_.catalogue, store);
        }

    cancelled_.store(false);
    progress_.store(0.0);
    tried_ = proposal;
    setStage(Stage::trying, "J'essaie le bus à blanc, sur des copies du morceau…");
    startTimer(progressPollMs);

    auto measured = std::make_shared<std::array<std::unique_ptr<engine::MixRender::Measured>, 4>>();
    worker_ = std::thread{
        [this, alive = alive_, measured, proposed]
        {
            std::array<engine::MixRender*, 4> renders{
                before_.get(), after_.get(), probeWith_.get(), probeWithout_.get()};
            for (std::size_t index = 0; index < renders.size(); ++index)
            {
                if (renders[index] == nullptr)
                    continue;
                (*measured)[index] =
                    renders[index]->run(cancelled_,
                                        [this, index](double done)
                                        { progress_.store((static_cast<double>(index) + done) / 4.0); });
            }
            juce::MessageManager::callAsync(
                [this, alive, measured, proposed]
                {
                    if (!alive->load())
                        return;
                    stopTimer();
                    if ((*measured)[0] == nullptr || (*measured)[1] == nullptr)
                    {
                        clearTry();
                        setStage(Stage::failed, "L'essai du bus n'a pas abouti.");
                        return;
                    }
                    beforeFile_ = before_->releaseFile();
                    afterFile_ = after_->releaseFile();
                    proposedFlux_ = after_->fluxCaptured();
                    proposedState_ = proposed;

                    Tried result;
                    result.lufsBefore = (*measured)[0]->master.integratedLufs;
                    result.lufsAfter = (*measured)[1]->master.integratedLufs;
                    const auto beforeAudio = readAll(beforeFile_);
                    result.differenceDb = apartDb(readAll(afterFile_), beforeAudio);
                    result.said.push_back(proposals_[*tried_].sentence);
                    result.said.push_back("Essai à blanc : " + french(result.lufsBefore) + " LUFS avant, " +
                                          french(result.lufsAfter) +
                                          " après ; l'après s'écarte de l'avant de " +
                                          french(result.differenceDb) + " dB.");
                    if ((*measured)[2] != nullptr && (*measured)[3] != nullptr && probeWith_ && probeWithout_)
                    {
                        const auto with = readAll(probeWith_->releaseFile());
                        const auto without = readAll(probeWithout_->releaseFile());
                        result.dryDb = shareDb(with, without);
                        if (result.dryDb && *result.dryDb > -20.0)
                            result.said.push_back("Ton effet laisse passer le son sec (" +
                                                  french(*result.dryDb) +
                                                  " dB mesurés) : sur le bus, mets-le à 100 % d'effet.");
                    }
                    before_.reset();
                    after_.reset();
                    probeWith_.reset();
                    probeWithout_.reset();
                    if (comparison_ != nullptr)
                        static_cast<void>(
                            comparison_->load(beforeFile_, result.lufsBefore, afterFile_, result.lufsAfter));
                    result_ = std::move(result);
                    setStage(Stage::tried, result_->said.back());
                });
        }};
}

std::vector<float> BusSession::proposedSound(const ui::FluxHost::Place& place) const
{
    std::vector<float> samples;
    for (const bool companion : {false, true})
    {
        if ((place.way == ui::FluxHost::Way::instrument && companion) ||
            (place.way == ui::FluxHost::Way::recordings && !companion))
            continue;
        const auto found = proposedFlux_.find({place.strip, place.slot, companion});
        if (found == proposedFlux_.end())
            continue;
        samples.resize(std::max(samples.size(), found->second.size()), 0.0f);
        for (std::size_t index = 0; index < found->second.size(); ++index)
            samples[index] += found->second[index];
    }
    return samples;
}

void BusSession::listen(bool after)
{
    if (comparison_ != nullptr && stage_ == Stage::tried)
        comparison_->play(0.0, after);
}

void BusSession::stopListening()
{
    if (comparison_ != nullptr)
        comparison_->stop();
}

bool BusSession::keep()
{
    if (stage_ != Stage::tried || !tried_ || *tried_ >= proposals_.size())
        return false;
    stopListening();
    const auto& shared = proposals_[*tried_];
    const auto name =
        shared.way == domain::buses::Way::send ? std::string{"Bus d'envoi"} : std::string{"Bus de groupe"};
    domain::GroupOptions group{};
    group.label = shared.way == domain::buses::Way::send ? "bus d'envoi" : "bus de groupe";
    const auto kept =
        wiring_.bus.executeGroup(domain::buses::compile(shared, proposedBus_, name, proposedPlugin_), group)
            .ok();
    clearTry();
    proposals_.clear();
    setStage(Stage::idle, kept ? "Bus gardé, en un seul pas d'historique." : "Le bus n'a pas pu être écrit.");
    return kept;
}

void BusSession::refuse()
{
    clearTry();
    setStage(Stage::idle, "Bus refusé : rien n'est écrit.");
}

} // namespace daw::app
