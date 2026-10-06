#include "daw/engine/AudioOutputKeeper.h"

#include <utility>

namespace daw::engine
{
namespace
{

constexpr int lookEveryMs = 1000;

// The last resort when a card goes: the shared driver opens whatever the
// exclusive one could not take.
const juce::String sharedType{"Windows Audio"};

} // namespace

AudioOutputKeeper::AudioOutputKeeper(juce::AudioDeviceManager& devices)
    : devices_(devices)
{
    if (auto* device = devices_.getCurrentAudioDevice(); device != nullptr)
    {
        preferred_ = device->getName();
        typeName_ = device->getTypeName();
    }
    else
    {
        typeName_ = devices_.getCurrentAudioDeviceType();
    }
    devices_.addChangeListener(this);
    startTimer(lookEveryMs);
}

AudioOutputKeeper::~AudioOutputKeeper()
{
    stopTimer();
    devices_.removeChangeListener(this);
}

juce::String AudioOutputKeeper::outputName() const
{
    auto* device = devices_.getCurrentAudioDevice();
    return device != nullptr ? device->getName() : juce::String{};
}

void AudioOutputKeeper::prefer(const juce::String& type, const juce::String& name)
{
    typeName_ = type;
    preferred_ = name;
}

void AudioOutputKeeper::loseOutputForTest()
{
    devices_.closeAudioDevice();
}

void AudioOutputKeeper::changeListenerCallback(juce::ChangeBroadcaster*)
{
    check();
}

void AudioOutputKeeper::timerCallback()
{
    check();
}

void AudioOutputKeeper::check()
{
    // Opening a device sends change messages of its own.
    if (opening_)
        return;

    const auto current = outputName();
    if (current.isEmpty())
    {
        if (!lost_)
        {
            lost_ = true;
            if (onLost)
                onLost();
            say("sortie perdue");
        }

        // The first one if it is there, the default of Windows in its driver
        // otherwise, the shared driver's default last.
        const std::pair<juce::String, juce::String> candidates[] = {{typeName_, preferred_},
                                                                    {typeName_, windowsDefault(typeName_)},
                                                                    {sharedType, windowsDefault(sharedType)}};
        for (const auto& [type, name] : candidates)
        {
            if (name.isNotEmpty() && available(type, name) && open(type, name))
            {
                lost_ = false;
                ++reopened_;
                say("sortie : " + name);
                return;
            }
        }
        return;
    }

    if (preferred_.isEmpty())
        preferred_ = current;

    // On a fallback, and the first one is back.
    const auto currentType = devices_.getCurrentAudioDeviceType();
    if ((current != preferred_ || currentType != typeName_) && available(typeName_, preferred_) &&
        open(typeName_, preferred_))
    {
        ++reopened_;
        say("sortie : " + preferred_);
    }
}

bool AudioOutputKeeper::available(const juce::String& typeName, const juce::String& name) const
{
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type != nullptr && type->getTypeName() == typeName)
            return type->getDeviceNames(false).contains(name);
    }
    return false;
}

juce::String AudioOutputKeeper::windowsDefault(const juce::String& typeName) const
{
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type == nullptr || type->getTypeName() != typeName)
            continue;
        const auto names = type->getDeviceNames(false);
        const auto index = type->getDefaultDeviceIndex(false);
        return juce::isPositiveAndBelow(index, names.size()) ? names[index] : juce::String{};
    }
    return {};
}

bool AudioOutputKeeper::open(const juce::String& typeName, const juce::String& name)
{
    const juce::ScopedValueSetter<bool> guard{opening_, true};

    if (devices_.getCurrentAudioDeviceType() != typeName)
        devices_.setCurrentAudioDeviceType(typeName, false);

    auto setup = devices_.getAudioDeviceSetup();
    setup.outputDeviceName = name;
    // A headset's microphone goes with the headset: an input that is not
    // there any more would fail the whole setup.
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type != nullptr && type->getTypeName() == typeName &&
            !type->getDeviceNames(true).contains(setup.inputDeviceName))
            setup.inputDeviceName = {};
    }

    const auto error = devices_.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty())
    {
        juce::Logger::writeToLog("audio: « " + name + " » ne s'ouvre pas : " + error);
        return false;
    }
    return outputName() == name;
}

void AudioOutputKeeper::say(const juce::String& what)
{
    juce::Logger::writeToLog("audio: " + what);
    if (onChanged)
        onChanged(what);
}

} // namespace daw::engine
