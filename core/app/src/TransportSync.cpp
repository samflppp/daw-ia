#include "TransportSync.h"

#include "daw/domain/commands/TransportCommands.h"

#include <memory>

namespace daw::app
{

TransportSync::TransportSync(domain::CommandBus& bus,
                             const domain::ProjectState& state,
                             tracktion::Edit& edit)
    : bus_(bus)
    , state_(state)
    , edit_(edit)
{
    token_ = bus_.addObserver(*this);
    startTimer(idleIntervalMs);
}

TransportSync::~TransportSync()
{
    stopTimer();
    bus_.removeObserver(token_);
}

void TransportSync::onExecuted(const domain::Receipt& receipt)
{
    if (receipt.type != domain::TransportPlay::commandType)
        return;

    // The projector has already run: it observes the bus and it was registered
    // first, so TransportControl::play() has returned by the time this is
    // called. What is left to wait for is the device.
    measuring_ = true;
    askedAtMs_ = juce::Time::getMillisecondCounterHiRes();
    positionAtAsk_ = edit_.getTransport().getPosition().inSeconds();
    startTimer(measuringIntervalMs);
}

void TransportSync::onCoalesced(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
}

void TransportSync::onUndone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
}

void TransportSync::onRedone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
}

void TransportSync::timerCallback()
{
    const auto& control = edit_.getTransport();
    const auto enginePlaying = control.isPlaying();

    if (measuring_)
    {
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - askedAtMs_;

        // The playhead moving is the first thing that can only happen once the
        // device is pulling audio: it is the closest an observer on this side
        // can get to the first sample leaving the machine.
        if (enginePlaying && control.getPosition().inSeconds() > positionAtAsk_)
        {
            juce::Logger::writeToLog("transport: playing after " + juce::String(elapsed, 1) + " ms");
            measuring_ = false;
            startTimer(idleIntervalMs);
        }
        else if (elapsed > startTimeoutMs)
        {
            juce::Logger::writeToLog("transport: still not playing after " + juce::String(elapsed, 1) +
                                     " ms");
            measuring_ = false;
            startTimer(idleIntervalMs);
        }

        return;
    }

    // Only in that one direction. The domain asking for playback and the
    // engine not having started yet is a normal fraction of a second, and
    // issuing a stop then would fight the command that just arrived.
    if (enginePlaying || !state_.transport().playing)
        return;

    static_cast<void>(bus_.execute(std::make_unique<domain::TransportStop>()));
}

} // namespace daw::app
