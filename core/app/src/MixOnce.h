#pragma once

#include "CopilotBridge.h"
#include "MixSession.h"

#include <juce_events/juce_events.h>

#include <functional>

namespace daw::app
{

// --mix-once (S21): one mix by the model, on the project the application
// opened, then out. What a person gets from « Mixer », minus the listening:
// the copilot's process asked, the guards, the trial render, and every line
// of it in daw.log — the proposal, what the guards refused, the tokens read
// and written. The proposal is refused, never kept: the project is left as it
// was.
//
// It is not a verification. It checks nothing, it calls the API and costs
// money, and it never runs in CI: a verification mixes by the rules only.
// Its first use is the first real mix by the model, on the project
// --verify-mix leaves behind; its next, the founder's three songs.
class MixOnce final : private juce::Timer
{
public:
    MixOnce(MixSession& mix, CopilotBridge& copilot, std::function<void()> done);
    ~MixOnce() override;

    MixOnce(const MixOnce&) = delete;
    MixOnce& operator=(const MixOnce&) = delete;
    MixOnce(MixOnce&&) = delete;
    MixOnce& operator=(MixOnce&&) = delete;

private:
    void timerCallback() override;
    void finish(const juce::String& why);

    MixSession& mix_;
    CopilotBridge& copilot_;
    std::function<void()> done_;
    double startedAtMs_{0.0};
    bool started_{false};
};

} // namespace daw::app
