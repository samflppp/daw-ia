#include "daw/engine/AudioOutputKeeper.h"

namespace daw::engine
{
namespace
{

constexpr int lookEveryMs = 1000;

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

        // The first one if it is there, the default of Windows otherwise.
        for (const auto& candidate : {preferred_, windowsDefault()})
        {
            if (candidate.isNotEmpty() && available(candidate) && open(candidate))
            {
                lost_ = false;
                ++reopened_;
                say("sortie : " + candidate);
                return;
            }
        }
        return;
    }

    if (preferred_.isEmpty())
        preferred_ = current;

    // On a fallback, and the first one is back.
    if (current != preferred_ && available(preferred_) && open(preferred_))
    {
        ++reopened_;
        say("sortie : " + preferred_);
    }
}

bool AudioOutputKeeper::available(const juce::String& name) const
{
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type != nullptr && type->getTypeName() == typeName_)
            return type->getDeviceNames(false).contains(name);
    }
    return false;
}

juce::String AudioOutputKeeper::windowsDefault() const
{
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type == nullptr || type->getTypeName() != typeName_)
            continue;
        const auto names = type->getDeviceNames(false);
        const auto index = type->getDefaultDeviceIndex(false);
        return juce::isPositiveAndBelow(index, names.size()) ? names[index] : juce::String{};
    }
    return {};
}

bool AudioOutputKeeper::open(const juce::String& name)
{
    const juce::ScopedValueSetter<bool> guard{opening_, true};

    if (devices_.getCurrentAudioDeviceType() != typeName_)
        devices_.setCurrentAudioDeviceType(typeName_, false);

    auto setup = devices_.getAudioDeviceSetup();
    setup.outputDeviceName = name;
    // A headset's microphone goes with the headset: an input that is not
    // there any more would fail the whole setup.
    for (auto* type : devices_.getAvailableDeviceTypes())
    {
        if (type != nullptr && type->getTypeName() == typeName_ &&
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
