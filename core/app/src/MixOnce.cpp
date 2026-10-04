#include "MixOnce.h"

#include <cstdint>

namespace daw::app
{
namespace
{

using Stage = ui::MixHost::Stage;

constexpr int pollMs = 250;
constexpr double copilotWaitMs = 60000.0;
constexpr double mixWaitMs = 600000.0;

} // namespace

MixOnce::MixOnce(MixSession& mix, CopilotBridge& copilot, std::function<void()> done)
    : mix_(mix)
    , copilot_(copilot)
    , done_(std::move(done))
{
    juce::Logger::writeToLog("mix-once: waiting for the copilot's process");
    startedAtMs_ = juce::Time::getMillisecondCounterHiRes();
    startTimer(pollMs);
}

MixOnce::~MixOnce()
{
    stopTimer();
}

void MixOnce::timerCallback()
{
    const auto waited = juce::Time::getMillisecondCounterHiRes() - startedAtMs_;

    if (!started_)
    {
        if (!copilot_.canInterpret())
        {
            if (waited > copilotWaitMs)
                finish("no copilot after 60 s: is DAW_IA_ANTHROPIC_API_KEY set?");
            return;
        }
        juce::Logger::writeToLog("mix-once: the copilot is there, mixing by the model");
        mix_.setUseModel(true);
        mix_.start();
        started_ = true;
        startedAtMs_ = juce::Time::getMillisecondCounterHiRes();
        return;
    }

    const auto stage = mix_.stage();
    if (stage == Stage::ready)
    {
        const auto* proposal = mix_.proposal();
        const auto by = proposal != nullptr ? proposal->decidedBy : std::string{"rien"};
        const auto& usage = mix_.usage();
        juce::Logger::writeToLog(
            "mix-once: decided by " + juce::String::fromUTF8(by.c_str()) + ", " +
            juce::String(proposal != nullptr ? static_cast<int>(proposal->changes.size()) : 0) +
            " settings, " + juce::String(static_cast<int>(mix_.refused().size())) + " refused; tokens " +
            juce::String(usage.inputTokens) + " in, " + juce::String(usage.outputTokens) + " out");
        mix_.reject();
        finish("done, the proposal refused: the project is untouched");
        return;
    }
    if (stage == Stage::failed)
    {
        finish("the mix failed: " + juce::String::fromUTF8(mix_.status().c_str()));
        return;
    }
    if (waited > mixWaitMs)
    {
        mix_.cancel();
        finish("no proposal after 10 minutes");
    }
}

void MixOnce::finish(const juce::String& why)
{
    stopTimer();
    juce::Logger::writeToLog("mix-once: " + why);
    if (done_)
        done_();
}

} // namespace daw::app
