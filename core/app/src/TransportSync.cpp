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
    remember("transport.play vu, départ attendu");
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
            remember("moteur parti après " + juce::String(elapsed, 1).toStdString() + " ms");
            measuring_ = false;
            startTimer(idleIntervalMs);
        }
        else if (elapsed > startTimeoutMs)
        {
            juce::Logger::writeToLog("transport: still not playing after " + juce::String(elapsed, 1) +
                                     " ms");
            remember("moteur toujours arrêté après " + juce::String(elapsed, 1).toStdString() + " ms");
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

    // Written down every time: this is the one command the application
    // issues that no hand asked for, and it lands in the middle of whatever
    // the user is doing while the song plays.
    const auto stopped = bus_.execute(std::make_unique<domain::TransportStop>());
    remember(stopped ? "arrêt du moteur porté au domaine"
                     : "arrêt du moteur refusé : " + stopped.error().message);
    juce::Logger::writeToLog("transport: engine stopped on its own, " +
                             juce::String(stopped ? "domain told" : "domain refused: ") +
                             juce::String::fromUTF8(stopped ? "" : stopped.error().message.c_str()));
}

void TransportSync::remember(std::string what)
{
    last_.what = std::move(what);
    last_.atMs = juce::Time::getMillisecondCounterHiRes();
}

} // namespace daw::app
