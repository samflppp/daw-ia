#include "daw/engine/ParameterBridge.h"

#include "daw/domain/commands/PluginCommands.h"

#include <algorithm>

namespace daw::engine
{
namespace
{

const juce::Identifier domainPluginIdProperty{"dawDomainPluginId"};

} // namespace

ParameterBridge::ParameterBridge(domain::CommandBus& bus,
                                 const domain::ProjectState& state,
                                 tracktion::Edit& edit,
                                 const ProjectProjector& projector)
    : bus_{bus}
    , state_{state}
    , edit_{edit}
    , projector_{projector}
{
    bus_.addObserver(*this);
    refresh();
}

ParameterBridge::~ParameterBridge()
{
    cancelPendingUpdate();
    unsubscribeAll();
}

void ParameterBridge::onExecuted(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    refresh();
}

void ParameterBridge::onCoalesced(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    refresh();
}

void ParameterBridge::onUndone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    refresh();
}

void ParameterBridge::onRedone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    refresh();
}

void ParameterBridge::unsubscribeAll()
{
    for (auto& subscription : subscriptions_)
    {
        if (subscription.parameter != nullptr)
            subscription.parameter->removeListener(this);
    }
    subscriptions_.clear();
}

const ParameterBridge::Subscription* ParameterBridge::find(tracktion::AutomatableParameter* parameter) const
{
    const auto found = std::find_if(subscriptions_.begin(),
                                    subscriptions_.end(),
                                    [parameter](const Subscription& subscription)
                                    { return subscription.parameter == parameter; });
    return found == subscriptions_.end() ? nullptr : &*found;
}

void ParameterBridge::refresh()
{
    // Rebuilding the table is O(plugins × parameters) and happens once per
    // command, not once per audio block. Keeping it exact is worth more than
    // keeping it cheap: a stale entry would mean a movement attributed to a
    // plugin that no longer exists.
    unsubscribeAll();

    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track == nullptr)
            continue;

        for (auto plugin : track->pluginList.getPlugins())
        {
            if (plugin == nullptr)
                continue;

            const auto marker = plugin->state.getProperty(domainPluginIdProperty).toString();
            if (marker.isEmpty())
                continue; // not a plugin the domain owns: nothing to report

            auto pluginId = domain::PluginId::parse(marker.toStdString());
            if (!pluginId)
                continue;

            const auto count = plugin->getNumAutomatableParameters();
            for (int index = 0; index < count; ++index)
            {
                auto parameter = plugin->getAutomatableParameter(index);
                if (parameter == nullptr)
                    continue;

                Subscription subscription{};
                subscription.parameter = parameter.get();
                subscription.pluginId = pluginId.value();
                subscription.paramId = parameter->paramID.toStdString();
                subscriptions_.push_back(std::move(subscription));

                parameter->addListener(this);
            }
        }
    }
}

void ParameterBridge::push(const Movement& movement)
{
    // First echo guard: while the projector writes, whatever a plugin says is
    // the projection talking back.
    if (projector_.isProjecting())
        return;

    const auto write = write_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % ringCapacity;
    if (next == read_.load(std::memory_order_acquire))
    {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    ring_[static_cast<std::size_t>(write)] = movement;
    write_.store(next, std::memory_order_release);

    triggerAsyncUpdate();
}

void ParameterBridge::parameterChanged(tracktion::AutomatableParameter& parameter, float newValue)
{
    push(Movement{Movement::Kind::value, &parameter, newValue});
}

void ParameterBridge::parameterChangeGestureBegin(tracktion::AutomatableParameter& parameter)
{
    push(Movement{Movement::Kind::gestureBegin, &parameter, 0.0f});
}

void ParameterBridge::parameterChangeGestureEnd(tracktion::AutomatableParameter& parameter)
{
    push(Movement{Movement::Kind::gestureEnd, &parameter, 0.0f});
}

void ParameterBridge::handleAsyncUpdate()
{
    auto read = read_.load(std::memory_order_relaxed);
    const auto write = write_.load(std::memory_order_acquire);

    while (read != write)
    {
        const auto movement = ring_[static_cast<std::size_t>(read)];
        read = (read + 1) % ringCapacity;

        const auto* subscription = find(movement.parameter);
        if (subscription == nullptr)
            continue; // the plugin went away between the movement and here

        switch (movement.kind)
        {
        case Movement::Kind::gestureBegin:
            if (!gesture_.has_value())
                gesture_ = bus_.beginGesture("plugin.param");
            break;

        case Movement::Kind::gestureEnd:
            if (gesture_.has_value())
            {
                static_cast<void>(bus_.endGesture(gesture_.value()));
                gesture_.reset();
            }
            break;

        case Movement::Kind::value:
        {
            const auto* plugin = state_.findPlugin(subscription->pluginId);
            if (plugin == nullptr)
                break;

            // Third echo guard, and the one that actually closes the loop: a
            // value the project already holds produces no command, so a
            // projection can never come back as history.
            const auto* existing = plugin->findParam(subscription->paramId);
            const auto value = static_cast<double>(juce::jlimit(0.0f, 1.0f, movement.value));
            if (existing != nullptr && juce::approximatelyEqual(existing->value, value))
                break;

            domain::ExecuteOptions options{};
            options.gesture = gesture_;

            static_cast<void>(bus_.execute(std::make_unique<domain::SetPluginParameter>(
                                               subscription->pluginId, subscription->paramId, value),
                                           options));
            break;
        }
        }
    }

    read_.store(read, std::memory_order_release);
}

} // namespace daw::engine
