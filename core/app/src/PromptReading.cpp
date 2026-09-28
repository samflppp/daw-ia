#include "PromptReading.h"

#include <utility>

namespace daw::app
{

PromptReading::PromptReading(CopilotBridge& copilot)
    : routed_(ui::RoutedPromptReader::Remote{[&copilot] { return copilot.canInterpret(); },
                                             [&copilot](std::uint64_t ticket,
                                                        const std::string& text,
                                                        const Zone& zone,
                                                        ui::RoutedPromptReader::Answered answered)
                                             { copilot.interpret(ticket, text, zone, std::move(answered)); }})
{
}

PromptReading::~PromptReading()
{
    stopTimer();
    routed_.cancel();
}

void PromptReading::read(std::string text, Zone zone, Done done)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    routed_.read(std::move(text),
                 std::move(zone),
                 [this, started, finish = std::move(done)](Reading reading)
                 {
                     stopTimer();
                     juce::Logger::writeToLog(
                         juce::String("generation: prompt read ") +
                         (reading.remote ? "by the copilot" : "locally") + " in " +
                         juce::String(juce::Time::getMillisecondCounterHiRes() - started, 0) + " ms");
                     if (finish)
                         finish(std::move(reading));
                 });

    // Answered already (the local words): nothing to wait for.
    if (routed_.reading())
        startTimer(patienceMs);
}

void PromptReading::cancel()
{
    stopTimer();
    routed_.cancel();
}

void PromptReading::timerCallback()
{
    stopTimer();
    routed_.expire();
}

} // namespace daw::app
