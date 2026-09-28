#pragma once

#include "CopilotBridge.h"
#include "daw/ui/model/PromptReader.h"

#include <juce_events/juce_events.h>

namespace daw::app
{

// The generation window's reader, in the application: the copilot's model
// through the bridge, the local words when it is not there, fails, or takes
// longer than a person waits for a few words to be understood.
class PromptReading final : public ui::PromptReader, private juce::Timer
{
public:
    // Past this, the window stops waiting and the local words answer. Long
    // enough for a model to read a sentence, short enough that nobody wonders
    // whether the application froze.
    static constexpr int patienceMs = 8000;

    explicit PromptReading(CopilotBridge& copilot);
    ~PromptReading() override;

    PromptReading(const PromptReading&) = delete;
    PromptReading& operator=(const PromptReading&) = delete;
    PromptReading(PromptReading&&) = delete;
    PromptReading& operator=(PromptReading&&) = delete;

    void read(std::string text, Zone zone, Done done) override;
    void cancel() override;
    [[nodiscard]] bool reading() const override { return routed_.reading(); }

private:
    void timerCallback() override;

    ui::RoutedPromptReader routed_;
};

} // namespace daw::app
