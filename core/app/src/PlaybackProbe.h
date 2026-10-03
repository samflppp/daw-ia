#pragma once

#include "TransportSync.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"

#include <tracktion_engine/tracktion_engine.h>

#include <cstddef>
#include <deque>
#include <string>

namespace daw::app
{

// The instrument for the intermittent bug of S12 and S13: an action taken
// while the song plays that has no effect, once in five runs, never on demand.
//
// It does not fix anything. It writes down, at the moment an action is
// refused, everything the two suspects leave behind:
//
//   - the bus: what was refused and why, from which thread, whether another
//     command was being applied, the open gesture, the depth of each stack,
//     and the last commands it applied, so an undo that took back the wrong
//     entry shows what slipped in;
//   - the transport: what the domain says and what the engine does, and the
//     last thing TransportSync saw or pushed, with its age.
//
// Every refusal goes to the log. The verification also reads describe() when
// a check fails, so the report holds the state next to the failure.
class PlaybackProbe final : private domain::BusObserver
{
public:
    PlaybackProbe(domain::CommandBus& bus,
                  const domain::ProjectState& state,
                  tracktion::Edit& edit,
                  const TransportSync& sync);
    ~PlaybackProbe() override;

    PlaybackProbe(const PlaybackProbe&) = delete;
    PlaybackProbe& operator=(const PlaybackProbe&) = delete;
    PlaybackProbe(PlaybackProbe&&) = delete;
    PlaybackProbe& operator=(PlaybackProbe&&) = delete;

    // One line: the transport, TransportSync, the bus, the recent commands.
    [[nodiscard]] std::string describe() const;

    // One line on the audio path, for a song that plays and is not heard
    // (S20, steps 58 and 60: every meter at -100 dBFS, the engine playing):
    // the device and whether Tracktion's stream time moves, the playback
    // context and its graph, every tap with the blocks it has seen, and every
    // track of the Edit with its mute, solo, clips and plugins. Read twice, a
    // moment apart, it says where the sound stops.
    [[nodiscard]] std::string describeAudio() const;

    // Refusals seen since the start, and the last ones as written to the log.
    [[nodiscard]] std::size_t refusalCount() const noexcept { return refusalCount_; }
    [[nodiscard]] const std::deque<std::string>& recentRefusals() const noexcept { return refusals_; }

private:
    static constexpr std::size_t kept = 8;

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;

    void remember(const char* verb, const domain::Receipt& receipt);
    void refused(const domain::Refusal& refusal);

    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    tracktion::Edit& edit_;
    const TransportSync& sync_;
    domain::ObserverToken token_{};

    std::deque<std::string> applied_;
    std::deque<std::string> refusals_;
    std::size_t refusalCount_{0};
};

} // namespace daw::app
