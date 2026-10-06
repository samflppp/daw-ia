#pragma once

#include "daw/domain/command/BusObserver.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <string>

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
    // The last thing this saw or did, and when, on the millisecond counter.
    // Read by PlaybackProbe when an action is refused during playback: the
    // intermittent "no effect" of S12 and S13 looks like a race with the stop
    // this pushes, and this is the half of the evidence only it holds.
    struct Event
    {
        std::string what{"rien encore"};
        double atMs{0.0};
    };

    TransportSync(domain::CommandBus& bus, const domain::ProjectState& state, tracktion::Edit& edit);
    ~TransportSync() override;

    TransportSync(const TransportSync&) = delete;
    TransportSync& operator=(const TransportSync&) = delete;
    TransportSync(TransportSync&&) = delete;
    TransportSync& operator=(TransportSync&&) = delete;

    [[nodiscard]] const Event& lastEvent() const noexcept { return last_; }

    // When the sound card last stopped or started, on the millisecond
    // counter (S24): see the tick.
    void watchCard(std::function<double()> changedAtMs) { cardChangedAtMs_ = std::move(changedAtMs); }

private:
    // Ten times a second is enough to notice a stop. While a start is being
    // waited for, the tick is short instead, because that is the number being
    // measured.
    static constexpr int idleIntervalMs = 100;
    static constexpr int measuringIntervalMs = 4;

    // Past this, the engine is not starting: report it and stop waiting rather
    // than tick at four milliseconds forever.
    static constexpr double startTimeoutMs = 3000.0;

    // How long a card reopening may keep the engine stopped, and how long a
    // card started again plays before the engine is started on it.
    static constexpr double cardGraceMs = 1500.0;
    static constexpr double cardStartedMs = 200.0;

    void timerCallback() override;

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;

    void remember(std::string what);

    std::function<double()> cardChangedAtMs_;
    double restartedFor_{0.0};
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    tracktion::Edit& edit_;
    domain::ObserverToken token_{};

    // The stopwatch of a start being waited for.
    bool measuring_{false};
    double askedAtMs_{0.0};
    double positionAtAsk_{0.0};

    Event last_{};
};

} // namespace daw::app
