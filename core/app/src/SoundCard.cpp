#include "SoundCard.h"

#include "daw/domain/live/Router.h"
#include "daw/engine/LiveInput.h"

#include <algorithm>
#include <utility>

namespace daw::app
{
namespace
{

constexpr int lookEveryMs = 1000;

std::vector<std::string> toStrings(const juce::StringArray& names)
{
    std::vector<std::string> out;
    for (const auto& name : names)
        out.push_back(name.toStdString());
    return out;
}

} // namespace

SoundCard::SoundCard(engine::AudioSettings& settings,
                     tracktion::Edit& edit,
                     std::function<bool()> songPlaying)
    : settings_(settings)
    , edit_(edit)
    , songPlaying_(std::move(songPlaying))
    , shown_(settings.current())
    , changedAt_(domain::live::now())
{
    settings_.onChanged = [this]
    {
        shown_ = settings_.current();
        changedAt_ = domain::live::now();
        settings_.resetTiming();
        sendChangeMessage();
    };
    startTimer(lookEveryMs);
}

SoundCard::~SoundCard()
{
    stopTimer();
    settings_.onChanged = nullptr;
}

std::vector<std::string> SoundCard::types() const
{
    return toStrings(settings_.types());
}

std::vector<std::string> SoundCard::outputs(const std::string& type) const
{
    return toStrings(settings_.outputs(juce::String::fromUTF8(type.c_str())));
}

std::vector<int> SoundCard::buffers(const std::string& type, const std::string& output) const
{
    return settings_.buffers(juce::String::fromUTF8(type.c_str()), juce::String::fromUTF8(output.c_str()));
}

ui::AudioHost::Choice SoundCard::current() const
{
    const auto now = settings_.current();
    return {now.type.toStdString(), now.output.toStdString(), now.buffer};
}

ui::AudioHost::Latency SoundCard::latency() const
{
    Latency latency;
    latency.outputSeconds = settings_.outputLatencySeconds();
    latency.blocks = settings_.timing();

    double lastPlayed = 0.0;
    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        for (auto* input : track->pluginList.getPluginsOfType<engine::LiveInputPlugin>())
        {
            if (input == nullptr)
                continue;
            if (!latency.playMeasured)
                latency.playSeconds = std::max(latency.playSeconds, input->wait());
            const auto placed = input->lastPlaced();
            if (placed.played > changedAt_ && placed.played > lastPlayed && placed.rendered >= placed.played)
            {
                lastPlayed = placed.played;
                latency.playSeconds = placed.rendered - placed.played;
                latency.playMeasured = true;
            }
        }
    }
    return latency;
}

void SoundCard::apply(const Choice& choice)
{
    settings_.apply({juce::String::fromUTF8(choice.type.c_str()),
                     juce::String::fromUTF8(choice.output.c_str()),
                     choice.buffer});
}

std::string SoundCard::whyNoTrial() const
{
    if (settings_.trialRunning())
        return "un essai est en cours";
    if (songPlaying_ && songPlaying_())
        return "arrête le morceau pour tester la carte : l'essai coupe le son";
    return {};
}

void SoundCard::startTrial()
{
    if (whyNoTrial().empty())
        settings_.startTrial();
}

void SoundCard::timerCallback()
{
    if (const auto now = settings_.current(); !(now == shown_))
    {
        shown_ = now;
        changedAt_ = domain::live::now();
        settings_.resetTiming();
        sendChangeMessage();
    }
}

} // namespace daw::app
