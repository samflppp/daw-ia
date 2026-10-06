#include "daw/engine/AudioSettings.h"

#include "daw/domain/serialization/Json.h"

#include <algorithm>
#include <memory>

namespace daw::engine
{
namespace
{

// How long a card just opened has to play its first block.
constexpr int heardWithinMs = 500;

// A setup tried settles for a second, then its blocks are measured for
// three: the opening's own hiccups are not the card's regime.
constexpr double trialSettleMs = 1000.0;
constexpr double trialMeasureMs = 3000.0;
constexpr int trialTickMs = 50;

// The window's sentences are French: their literals are UTF-8, which a
// juce::String built from a plain char pointer would read as ASCII.
juce::String u8(const char* text)
{
    return juce::String::fromUTF8(text);
}

juce::String describe(const AudioSettings::Choice& choice)
{
    return u8("« ") + choice.output + u8(" » (") + choice.type + ", " + juce::String(choice.buffer) +
           u8(" échantillons)");
}

} // namespace

// --- the clock ----------------------------------------------------------------

void BlockClock::audioDeviceIOCallbackWithContext(const float* const* inputs,
                                                  int inputCount,
                                                  float* const* outputs,
                                                  int outputCount,
                                                  int samples,
                                                  const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(inputs, inputCount, context);

    // Silence: JUCE sums every callback's buffer into what the card plays.
    for (int channel = 0; channel < outputCount; ++channel)
    {
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputs[channel], samples);
    }

    if (const auto asked = resetsAsked_.load(std::memory_order_acquire);
        asked != resetsDone_.load(std::memory_order_relaxed))
    {
        blocks_.store(0, std::memory_order_relaxed);
        intervals_.store(0, std::memory_order_relaxed);
        sumSeconds_.store(0.0, std::memory_order_relaxed);
        worstSeconds_.store(0.0, std::memory_order_relaxed);
        late_.store(0, std::memory_order_relaxed);
        lastTicks_.store(0, std::memory_order_relaxed);
        resetsDone_.store(asked, std::memory_order_release);
    }

    const auto now = juce::Time::getHighResolutionTicks();
    const auto last = lastTicks_.exchange(now, std::memory_order_relaxed);
    if (last > 0)
    {
        const auto interval = juce::Time::highResolutionTicksToSeconds(now - last);
        const auto block = samples / rate_.load(std::memory_order_relaxed);
        intervals_.store(intervals_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        sumSeconds_.store(sumSeconds_.load(std::memory_order_relaxed) + interval, std::memory_order_relaxed);
        if (interval > worstSeconds_.load(std::memory_order_relaxed))
            worstSeconds_.store(interval, std::memory_order_relaxed);
        if (domain::live::isLate(interval, block))
            late_.store(late_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
    lastSize_.store(samples, std::memory_order_relaxed);
    blocks_.store(blocks_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    seen_.fetch_add(1, std::memory_order_release);
}

void BlockClock::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    changedAtMs_.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        rate_.store(device->getCurrentSampleRate(), std::memory_order_relaxed);
    // A reopened card: its first block is not late after the last one of
    // the card before.
    lastTicks_.store(0, std::memory_order_relaxed);
}

void BlockClock::audioDeviceStopped()
{
    changedAtMs_.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    lastTicks_.store(0, std::memory_order_relaxed);
}

domain::live::BlockTiming BlockClock::timing() const noexcept
{
    domain::live::BlockTiming timing;
    // Until the writer has carried a reset out, the clock heard nothing since.
    if (resetsAsked_.load(std::memory_order_acquire) != resetsDone_.load(std::memory_order_acquire))
        return timing;
    timing.blocks = blocks_.load(std::memory_order_relaxed);
    timing.lastSize = lastSize_.load(std::memory_order_relaxed);
    const auto intervals = intervals_.load(std::memory_order_relaxed);
    timing.meanSeconds =
        intervals > 0 ? sumSeconds_.load(std::memory_order_relaxed) / static_cast<double>(intervals) : 0.0;
    timing.worstSeconds = worstSeconds_.load(std::memory_order_relaxed);
    timing.late = late_.load(std::memory_order_relaxed);
    return timing;
}

// --- the settings ---------------------------------------------------------------

AudioSettings::AudioSettings(juce::AudioDeviceManager& devices, AudioOutputKeeper& keeper, juce::File store)
    : devices_(devices)
    , keeper_(keeper)
    , store_(std::move(store))
{
    devices_.addAudioCallback(&clock_);
    loadStore();
}

AudioSettings::~AudioSettings()
{
    ticks_.stopTimer();
    stopTimer();
    devices_.removeAudioCallback(&clock_);
}

juce::StringArray AudioSettings::types() const
{
    juce::StringArray found;
    for (const auto wanted :
         {domain::live::sharedDriver, domain::live::lowLatencyDriver, domain::live::exclusiveDriver})
    {
        const auto name = juce::String(wanted.data(), wanted.size());
        for (auto* type : devices_.getAvailableDeviceTypes())
        {
            if (type != nullptr && type->getTypeName() == name)
                found.add(name);
        }
    }
    return found;
}

juce::StringArray AudioSettings::outputs(const juce::String& typeName) const
{
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type != nullptr && type->getTypeName() == typeName)
            return type->getDeviceNames(false);
    }
    return {};
}

std::vector<int> AudioSettings::buffers(const juce::String& typeName, const juce::String& output) const
{
    const auto now = current();
    if (auto* device = devices_.getCurrentAudioDevice();
        device != nullptr && now.type == typeName && now.output == output)
    {
        const auto sizes = device->getAvailableBufferSizes();
        return {sizes.begin(), sizes.end()};
    }

    const auto key = std::make_pair(typeName, output);
    if (const auto known = sizes_.find(key); known != sizes_.end())
        return known->second;

    std::vector<int> sizes;
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type == nullptr || type->getTypeName() != typeName)
            continue;
        // Made to be asked, never opened: no sound goes through it.
        if (const std::unique_ptr<juce::AudioIODevice> device{type->createDevice(output, {})};
            device != nullptr)
        {
            const auto offered = device->getAvailableBufferSizes();
            sizes.assign(offered.begin(), offered.end());
        }
    }
    sizes_[key] = sizes;
    return sizes;
}

AudioSettings::Choice AudioSettings::current() const
{
    Choice choice;
    choice.type = devices_.getCurrentAudioDeviceType();
    if (auto* device = devices_.getCurrentAudioDevice(); device != nullptr)
    {
        choice.output = device->getName();
        choice.buffer = device->getCurrentBufferSizeSamples();
    }
    return choice;
}

double AudioSettings::sampleRate() const
{
    auto* device = devices_.getCurrentAudioDevice();
    return device != nullptr ? device->getCurrentSampleRate() : 0.0;
}

double AudioSettings::outputLatencySeconds() const
{
    auto* device = devices_.getCurrentAudioDevice();
    if (device == nullptr || device->getCurrentSampleRate() <= 0.0)
        return 0.0;
    return device->getOutputLatencyInSamples() / device->getCurrentSampleRate();
}

domain::live::AudioAdvice AudioSettings::advice() const
{
    const auto now = current();
    return domain::live::advise(trials(), now.type.toStdString(), now.buffer);
}

std::vector<domain::live::CardTrial> AudioSettings::trials() const
{
    const auto found = kept_.find(current().output);
    return found != kept_.end() ? found->second : std::vector<domain::live::CardTrial>{};
}

double AudioSettings::trialProgress() const noexcept
{
    if (!trial_.has_value() || trial_->setups.empty())
        return 0.0;
    return static_cast<double>(trial_->index) / static_cast<double>(trial_->setups.size());
}

void AudioSettings::startTrial()
{
    if (trial_.has_value())
        return;
    Trial trial;
    trial.first = current();
    std::vector<domain::live::AudioDriver> drivers;
    for (const auto& type : types())
    {
        // The same output in every driver, when that driver has it.
        if (outputs(type).contains(trial.first.output))
            drivers.push_back({type.toStdString(), buffers(type, trial.first.output)});
    }
    trial.setups = domain::live::trialSetups(drivers);
    if (trial.setups.empty())
    {
        say(u8("Rien à essayer sur cette carte."));
        return;
    }
    trial_ = std::move(trial);
    say(u8("Essai de la carte : ") + juce::String(static_cast<int>(trial_->setups.size())) +
        u8(" réglages, le son coupé."));
    ticks_.startTimer(trialTickMs);
}

void AudioSettings::cancelTrial()
{
    if (trial_.has_value())
        finishTrial(true);
}

void AudioSettings::trialTick()
{
    if (!trial_.has_value())
    {
        ticks_.stopTimer();
        return;
    }
    auto& trial = *trial_;
    if (trial.index >= trial.setups.size())
    {
        finishTrial(false);
        return;
    }

    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto& setup = trial.setups[trial.index];
    if (!trial.opened)
    {
        trial.opened = true;
        const Choice choice{juce::String::fromUTF8(setup.type.c_str()), trial.first.output, setup.buffer};
        if (!apply(choice))
        {
            // Not opened: a setup that does not hold.
            domain::live::CardTrial failed;
            failed.type = setup.type;
            failed.buffer = setup.buffer;
            trial.results.push_back(failed);
            ++trial.index;
            trial.opened = false;
            return;
        }
        // Its own check would give the first setup back half a second after
        // a silent one; the trial measures that silence instead.
        stopTimer();
        pending_.reset();
        trial.settleUntilMs = now + trialSettleMs;
        trial.measuredFromMs = 0.0;
        return;
    }
    if (now < trial.settleUntilMs)
        return;
    if (trial.measuredFromMs == 0.0)
    {
        clock_.reset();
        trial.measuredFromMs = now;
        return;
    }
    if (now - trial.measuredFromMs < trialMeasureMs)
        return;

    const auto opened = current();
    domain::live::CardTrial result;
    result.type = opened.type.toStdString();
    result.buffer = opened.buffer;
    result.sampleRate = sampleRate();
    result.timing = clock_.timing();
    result.outputSeconds = outputLatencySeconds();
    juce::Logger::writeToLog(u8("audio: essai ") + opened.type + ", " + juce::String(opened.buffer) +
                             u8(" échantillons : ") + juce::String(result.timing.blocks) + " blocs, " +
                             juce::String(result.timing.late) + u8(" décrochages, au pire ") +
                             juce::String(result.timing.worstSeconds * 1000.0, 2) + " ms");
    trial.results.push_back(result);
    ++trial.index;
    trial.opened = false;
}

void AudioSettings::finishTrial(bool cancelled)
{
    ticks_.stopTimer();
    auto trial = std::move(*trial_);
    trial_.reset();
    apply(trial.first);
    if (cancelled)
    {
        say(u8("Essai annulé : la carte d'avant est rouverte."));
        return;
    }
    kept_[trial.first.output] = trial.results;
    saveStore();
    say(u8("Carte testée. ") + juce::String::fromUTF8(advice().sentence.c_str()));
}

void AudioSettings::loadStore()
{
    if (!store_.existsAsFile())
        return;
    const auto read = domain::json::read(store_.loadFileAsString().toStdString());
    if (!read.ok())
        return;
    const auto* cards = read.value().find("cards");
    if (cards == nullptr || cards->asObject() == nullptr)
        return;
    for (const auto& [output, trials] : *cards->asObject())
        kept_[juce::String::fromUTF8(output.c_str())] = domain::live::trialsFromValue(trials);
}

void AudioSettings::saveStore() const
{
    if (store_ == juce::File{})
        return;
    domain::Value::Object cards;
    for (const auto& [output, trials] : kept_)
        cards.emplace_back(output.toStdString(), domain::live::toValue(trials));
    const auto text = domain::json::writePretty(domain::Value::object(
        {{"cards", domain::Value::object(std::move(cards))}, {"version", domain::Value{1}}}));
    store_.getParentDirectory().createDirectory();
    if (!store_.replaceWithText(juce::String::fromUTF8(text.c_str())))
        juce::Logger::writeToLog("audio: " + store_.getFullPathName() + u8(" ne s'écrit pas"));
}

bool AudioSettings::open(const Choice& choice, juce::String& error)
{
    if (devices_.getCurrentAudioDeviceType() != choice.type)
        devices_.setCurrentAudioDeviceType(choice.type, true);

    auto setup = devices_.getAudioDeviceSetup();
    setup.outputDeviceName = choice.output;
    setup.inputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = true;
    setup.bufferSize = choice.buffer;

    error = devices_.setAudioDeviceSetup(setup, true);
    if (error.isEmpty() && current().output != choice.output)
        error = "une autre sortie s'est ouverte";
    return error.isEmpty();
}

bool AudioSettings::apply(const Choice& choice)
{
    // A choice made while the last one waits to be heard: the last one is
    // what plays now, and what a failure goes back to.
    stopTimer();
    pending_.reset();

    const auto previous = current();
    if (choice == previous)
    {
        say(u8("déjà en place : ") + describe(choice));
        return true;
    }

    if (beforeReopen)
        beforeReopen();
    keeper_.prefer(choice.type, choice.output);

    if (juce::String error; !open(choice, error))
    {
        keeper_.prefer(previous.type, previous.output);
        juce::String back;
        open(previous, back);
        say(describe(choice) + u8(" ne s'ouvre pas : ") + error + u8(". Retour à ") + describe(previous) +
            ".");
        return false;
    }

    const auto opened = current();
    say(opened.buffer == choice.buffer
            ? "ouvert : " + describe(opened) + "."
            : "ouvert : " + describe(opened) + ", la carte a pris " + juce::String(opened.buffer) +
                  u8(" échantillons au lieu de ") + juce::String(choice.buffer) + ".");
    pending_ = Pending{previous, opened, clock_.blocksSeen()};
    startTimer(heardWithinMs);
    return true;
}

void AudioSettings::timerCallback()
{
    stopTimer();
    if (!pending_.has_value())
        return;
    const auto pending = *pending_;
    pending_.reset();
    if (clock_.blocksSeen() > pending.seenAtOpen)
        return;

    if (beforeReopen)
        beforeReopen();
    keeper_.prefer(pending.previous.type, pending.previous.output);
    juce::String error;
    open(pending.previous, error);
    say(describe(pending.tried) + u8(" s'ouvre mais ne joue aucun bloc en ") + juce::String(heardWithinMs) +
        u8(" ms. Retour à ") + describe(pending.previous) + ".");
}

void AudioSettings::say(const juce::String& what)
{
    said_ = what;
    juce::Logger::writeToLog("audio: " + what);
    if (onChanged)
        onChanged();
}

} // namespace daw::engine
