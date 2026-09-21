#pragma once

#include "daw/domain/command/BusObserver.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

namespace daw::app
{

// Two jobs, both about the gap between what the domain says and what the
// engine does.
//
// 1. It tells the domain when the engine stopped on its own. Playback ends
//    when it runs off the end of the material, and Tracktion asks nobody.
//    Until now nothing carried that back, so ProjectState kept saying
//    "playing" over a silent engine. Two consequences, both seen in use: the
//    next transport.play was not a change and the button did nothing, and a
//    copilot reading the project would have been told the wrong thing.
//
// 2. It measures how long playback takes to start, and writes it to the log.
//    "There is a visible delay" is a report; a number is a fact one can act
//    on, and the two halves of that number -- the call that blocks the message
//    thread, and the wait for the device -- are fixed in different places.
//
// It is a Timer as well as a BusObserver because an observer may not call back
// into the bus: the bus refuses a reentrant call, by design since S2. The
// observer half only writes down the time.
//
// It issues transport.stop and nothing else. The command is transient, so this
// writes no history and no journal entry -- it corrects the domain, it does
// not invent an action the user did not take.
class TransportSync final : private juce::Timer, private domain::BusObserver
{
public:
    TransportSync(domain::CommandBus& bus, const domain::ProjectState& state, tracktion::Edit& edit);
    ~TransportSync() override;

    TransportSync(const TransportSync&) = delete;
    TransportSync& operator=(const TransportSync&) = delete;
    TransportSync(TransportSync&&) = delete;
    TransportSync& operator=(TransportSync&&) = delete;

private:
    // Ten times a second is enough to notice a stop. While a start is being
    // waited for, the tick is short instead, because that is the number being
    // measured.
    static constexpr int idleIntervalMs = 100;
    static constexpr int measuringIntervalMs = 4;

    // Past this, the engine is not starting: report it and stop waiting rather
    // than tick at four milliseconds forever.
    static constexpr double startTimeoutMs = 3000.0;

    void timerCallback() override;

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;

    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    tracktion::Edit& edit_;
    domain::ObserverToken token_{};

    // The stopwatch of a start being waited for.
    bool measuring_{false};
    double askedAtMs_{0.0};
    double positionAtAsk_{0.0};
};

} // namespace daw::app
