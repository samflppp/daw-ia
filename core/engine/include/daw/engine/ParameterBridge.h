#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ProjectProjector.h"

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daw::engine
{

// Turns a user's hand on a plugin's own knob into commands on the bus.
//
// Without this, half the project would be invisible to undo: the user opens the
// plugin window, turns a knob, and nothing in ProjectState knows. With it, a
// knob sweep is one history entry, exactly like a fader drag, and it is the
// existing gesture mechanism that makes it so — the bridge opens a gesture when
// the plugin says a gesture started, and closes it when the plugin says it
// ended. Not one line of the bus changes.
//
// Three things it has to get right, and they are the whole difficulty:
//
//   threads   a plugin reports a parameter change from the audio thread or from
//             its own. The bus refuses every thread but its own, and Tracktion
//             expects the message thread. So a change is recorded in a ring
//             with no lock and no allocation, and it becomes a command later, on
//             the message thread. The domain therefore sees the knob's
//             trajectory sampled, not every intermediate value — the value at
//             the end of the gesture, the one undo returns to, is exact.
//
//   echo      projecting a value makes the plugin report that same value back.
//             Left alone, that would create a command, then a projection, then
//             a command. Three guards, in this order: the projection writes
//             without notification, the bridge stays silent while the projector
//             is writing, and a change equal to what ProjectState already holds
//             produces nothing.
//
//   identity  a parameter is named by the format's own parameter id, never by
//             its index in a list, because that id is what goes in the journal.
class ParameterBridge final : private tracktion::AutomatableParameter::Listener, private juce::AsyncUpdater
{
public:
    // The projector is taken by non-const reference because the bridge hangs
    // itself on its onProjected callback: subscriptions have to be rebuilt
    // right after a projection, and nothing else knows when that happened.
    ParameterBridge(domain::CommandBus& bus,
                    const domain::ProjectState& state,
                    tracktion::Edit& edit,
                    ProjectProjector& projector);
    ~ParameterBridge() override;

    ParameterBridge(const ParameterBridge&) = delete;
    ParameterBridge& operator=(const ParameterBridge&) = delete;

    // Subscribes to the parameters of every hosted plugin the Edit now holds,
    // and drops the ones that are gone. Called after each projection; safe to
    // call at any time from the message thread.
    void refresh();

    // How many movements were dropped because the ring was full. Exposed so a
    // test can assert it stays at zero rather than trust that it does.
    [[nodiscard]] std::size_t droppedMovements() const noexcept { return dropped_.load(); }

private:
    struct Subscription
    {
        tracktion::AutomatableParameter* parameter{nullptr};
        domain::PluginId pluginId{};
        std::string paramId;
    };

    struct Movement
    {
        enum class Kind : std::uint8_t
        {
            value,
            gestureBegin,
            gestureEnd,
        };

        Kind kind{Kind::value};
        tracktion::AutomatableParameter* parameter{nullptr};
        float value{0.0f};
    };

    // --- tracktion parameter listener
    void curveHasChanged(tracktion::AutomatableParameter&) override {}
    void parameterChanged(tracktion::AutomatableParameter& parameter, float newValue) override;
    void parameterChangeGestureBegin(tracktion::AutomatableParameter& parameter) override;
    void parameterChangeGestureEnd(tracktion::AutomatableParameter& parameter) override;

    void handleAsyncUpdate() override;

    void push(const Movement& movement);
    void unsubscribeAll();
    [[nodiscard]] const Subscription* find(tracktion::AutomatableParameter* parameter) const;

    static constexpr int ringCapacity = 4096;

    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    tracktion::Edit& edit_;
    ProjectProjector& projector_;

    std::vector<Subscription> subscriptions_;

    std::array<Movement, ringCapacity> ring_{};
    std::atomic<int> write_{0};
    std::atomic<int> read_{0};
    std::atomic<std::size_t> dropped_{0};

    std::optional<domain::GestureId> gesture_;
};

} // namespace daw::engine
