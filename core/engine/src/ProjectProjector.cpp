#include "daw/engine/ProjectProjector.h"

#include "HostedParameters.h"

#include <algorithm>

namespace daw::engine
{
namespace
{

// The domain identifier, carried inside the Tracktion track state. This is the
// binding between the two worlds, and it is the only thing the Edit knows
// about the domain.
const juce::Identifier domainTrackIdProperty{"dawDomainTrackId"};

// Same idea one level down: a hosted plugin carries the domain identifier of the
// instance it projects, so a chain is bound by identity and never by position.
// Removing a plugin in the middle cannot make the projector write a state into
// the wrong plugin.
const juce::Identifier domainPluginIdProperty{"dawDomainPluginId"};

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str(), static_cast<int>(text.size()));
}

} // namespace

ProjectProjector::ProjectProjector(tracktion::Edit& edit,
                                   const domain::ProjectState& state,
                                   PluginCatalogue* catalogue,
                                   ContentStore* contentStore)
    : edit_{edit}
    , state_{state}
    , catalogue_{catalogue}
    , contentStore_{contentStore}
    , transport_{edit}
{
}

void ProjectProjector::onExecuted(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    reconcile();
}

void ProjectProjector::onCoalesced(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    reconcile();
}

void ProjectProjector::onUndone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    reconcile();
}

void ProjectProjector::onRedone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);
    reconcile();
}

tracktion::AudioTrack* ProjectProjector::findTrack(const domain::TrackId& id) const
{
    const auto wanted = toJuce(id.toString());

    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track != nullptr && track->state.getProperty(domainTrackIdProperty).toString() == wanted)
            return track;
    }
    return nullptr;
}

tracktion::AudioTrack* ProjectProjector::createTrackFor(const domain::TrackId& id)
{
    const auto before = tracktion::getAudioTracks(edit_);
    edit_.ensureNumberOfAudioTracks(before.size() + 1);

    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track != nullptr && !before.contains(track))
        {
            track->state.setProperty(domainTrackIdProperty, toJuce(id.toString()), nullptr);
            return track;
        }
    }
    return nullptr;
}

void ProjectProjector::removeUnknownTracks()
{
    // Any audio track without a known domain identifier is removed, including
    // the one Tracktion creates with a new Edit. The domain decides what
    // exists; the Edit never keeps a track of its own.
    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track == nullptr)
            continue;

        const auto marker = track->state.getProperty(domainTrackIdProperty).toString();
        const bool known = std::any_of(state_.tracks().begin(),
                                       state_.tracks().end(),
                                       [&marker](const domain::Track& source)
                                       { return toJuce(source.id.toString()) == marker; });
        if (!known)
            edit_.deleteTrack(track);
    }
}

void ProjectProjector::ensureInstrument(tracktion::AudioTrack& track, const domain::Track& source)
{
    // Without an instrument a MIDI clip is silent. 4OSC ships with Tracktion, so
    // a project makes a sound with no external plugin at all.
    //
    // It is a fallback, not a fixture: as soon as the track holds a plugin the
    // catalogue calls an instrument, 4OSC leaves. Keeping both would put a
    // second synth under the user's own, playing the same notes.
    const auto existing = track.pluginList.getPluginsOfType<tracktion::FourOscPlugin>();

    if (hasDomainInstrument(source))
    {
        for (auto plugin : existing)
        {
            if (plugin != nullptr)
                plugin->deleteFromParent();
        }
        return;
    }

    if (!existing.isEmpty())
        return;

    if (auto plugin = edit_.getPluginCache().createNewPlugin(tracktion::FourOscPlugin::xmlTypeName, {});
        plugin != nullptr)
    {
        track.pluginList.insertPlugin(plugin, 0, nullptr);
    }
}

bool ProjectProjector::isInstrument(const domain::PluginRef& ref) const
{
    if (catalogue_ == nullptr)
        return false;

    const auto description = catalogue_->find(ref);
    return description.has_value() && description->isInstrument;
}

bool ProjectProjector::hasDomainInstrument(const domain::Track& source) const
{
    return std::any_of(source.plugins.begin(),
                       source.plugins.end(),
                       [this](const domain::PluginInstance& plugin) { return isInstrument(plugin.ref); });
}

tracktion::Plugin* ProjectProjector::findPlugin(tracktion::AudioTrack& track, const domain::PluginId& id)
{
    const auto wanted = toJuce(id.toString());

    for (auto plugin : track.pluginList.getPlugins())
    {
        if (plugin != nullptr && plugin->state.getProperty(domainPluginIdProperty).toString() == wanted)
            return plugin;
    }
    return nullptr;
}

tracktion::Plugin::Ptr ProjectProjector::createPluginFor(const domain::PluginInstance& source)
{
    if (catalogue_ == nullptr)
        return {};

    const auto description = catalogue_->find(source.ref);
    if (!description.has_value())
    {
        // Named, not silently replaced. The application shows the list; the
        // project keeps the instance, so reinstalling the plugin is enough to
        // get the sound back.
        const auto missing = source.ref.format + ":" + source.ref.identifier;
        if (std::find(missing_.begin(), missing_.end(), missing) == missing_.end())
            missing_.push_back(missing);
        return {};
    }

    auto plugin =
        edit_.getPluginCache().createNewPlugin(tracktion::ExternalPlugin::xmlTypeName, *description);
    if (plugin == nullptr)
        return {};

    plugin->state.setProperty(domainPluginIdProperty, toJuce(source.id.toString()), nullptr);
    return plugin;
}

void ProjectProjector::removeUnknownPlugins(tracktion::AudioTrack& track, const domain::Track& source)
{
    for (auto plugin : track.pluginList.getPlugins())
    {
        if (plugin == nullptr)
            continue;

        const auto marker = plugin->state.getProperty(domainPluginIdProperty).toString();
        if (marker.isEmpty())
            continue; // volume, pan, the 4OSC fallback: not ours to remove here

        const bool known = std::any_of(source.plugins.begin(),
                                       source.plugins.end(),
                                       [&marker](const domain::PluginInstance& instance)
                                       { return toJuce(instance.id.toString()) == marker; });
        if (!known)
            plugin->deleteFromParent();
    }
}

void ProjectProjector::applyPluginState(tracktion::Plugin& target, const domain::PluginInstance& source)
{
    if (contentStore_ == nullptr)
        return;

    const auto previous = std::find_if(projectedStates_.begin(),
                                       projectedStates_.end(),
                                       [&source](const auto& entry) { return entry.first == source.id; });

    const bool alreadyProjected = previous != projectedStates_.end();
    if (alreadyProjected && previous->second == source.state.digest)
        return; // the digest has not moved: the plugin keeps what it holds

    if (!source.state.isEmpty())
    {
        auto bytes = contentStore_->get(source.state);
        if (!bytes)
            return; // a missing or damaged blob leaves the plugin as it is

        if (auto* external = dynamic_cast<tracktion::ExternalPlugin*>(&target); external != nullptr)
        {
            if (auto* instance = external->getAudioPluginInstance(); instance != nullptr)
                instance->setStateInformation(bytes.value().getData(),
                                              static_cast<int>(bytes.value().getSize()));
        }
    }

    if (alreadyProjected)
        previous->second = source.state.digest;
    else
        projectedStates_.emplace_back(source.id, source.state.digest);
}

void ProjectProjector::applyPluginParameters(tracktion::Plugin& target, const domain::PluginInstance& source)
{
    // The blob first, the sparse parameters over it: that order is the whole
    // point of keeping the two apart, and it is applied here and nowhere else.
    for (const auto& param : source.params)
    {
        // The plugin's own parameters, never Tracktion's dry and wet: see
        // HostedParameters.h for why the difference matters.
        auto* parameter = hostedParameter(target, param.paramId);
        if (parameter == nullptr)
            continue;

        const auto wanted = static_cast<float>(param.value);
        if (juce::approximatelyEqual(parameter->getCurrentNormalisedValue(), wanted))
            continue; // writing an unchanged value would only produce an echo

        // Notified on purpose, and it costs nothing: Tracktion only writes the
        // value into the plugin's ValueTree when it notifies, and rendering
        // builds its own copy of the Edit from that tree. Without the
        // notification the sound would follow the live instance and a render
        // would silently use the old value.
        //
        // The notification reaches the parameter bridge too, which is exactly
        // what the projecting flag is for.
        parameter->setNormalisedParameter(wanted, juce::sendNotificationSync);
    }
}

void ProjectProjector::reconcilePlugins(tracktion::AudioTrack& target, const domain::Track& source)
{
    removeUnknownPlugins(target, source);

    // The fallback synth, when there is one, stays in front of the chain: the
    // user's own plugins are placed after it, in the order the domain gives.
    const auto offset = target.pluginList.getPluginsOfType<tracktion::FourOscPlugin>().size();

    for (std::size_t index = 0; index < source.plugins.size(); ++index)
    {
        const auto& instance = source.plugins[index];

        auto* plugin = findPlugin(target, instance.id);
        if (plugin == nullptr)
        {
            auto created = createPluginFor(instance);
            if (created == nullptr)
                continue;

            target.pluginList.insertPlugin(created, offset + static_cast<int>(index), nullptr);
            plugin = created.get();
        }

        plugin->setEnabled(!instance.bypassed);
        applyPluginState(*plugin, instance);
        applyPluginParameters(*plugin, instance);
    }
}

void ProjectProjector::rebuildClips(tracktion::AudioTrack& target, const domain::Track& source)
{
    const auto existing = target.getClips();
    for (auto* clip : existing)
    {
        if (clip != nullptr)
            clip->removeFromParent();
    }

    for (const auto& clip : source.clips)
    {
        const tracktion::BeatRange beats{tracktion::BeatPosition::fromBeats(clip.startBeats),
                                         tracktion::BeatDuration::fromBeats(clip.lengthBeats)};

        auto midiClip =
            target.insertMIDIClip(toJuce(clip.id.toString()), edit_.tempoSequence.toTime(beats), nullptr);
        if (midiClip == nullptr)
            continue;

        auto& sequence = midiClip->getSequence();
        sequence.clear(nullptr);

        // Note positions are relative to the clip, the way Tracktion stores
        // them and the way the domain defines them.
        for (const auto& note : clip.notes)
        {
            sequence.addNote(note.pitch,
                             tracktion::BeatPosition::fromBeats(note.startBeats),
                             tracktion::BeatDuration::fromBeats(note.lengthBeats),
                             note.velocity,
                             0,
                             nullptr);
        }
    }
}

void ProjectProjector::reconcile()
{
    // Everything written into the Edit from here on is a projection, not a user
    // action. The parameter bridge watches this flag to tell the two apart.
    projecting_ = true;
    const struct Guard
    {
        bool& flag;
        ~Guard() { flag = false; }
    } guard{projecting_};

    if (auto* tempo = edit_.tempoSequence.getTempo(0); tempo != nullptr)
        tempo->setBpm(state_.tempo());

    removeUnknownTracks();

    std::vector<std::pair<domain::TrackId, domain::Value>> stillProjected;
    stillProjected.reserve(state_.tracks().size());

    for (const auto& source : state_.tracks())
    {
        auto* target = findTrack(source.id);
        if (target == nullptr)
            target = createTrackFor(source.id);
        if (target == nullptr)
            continue;

        const auto snapshot = source.toValue();

        const auto previous = std::find_if(projected_.begin(),
                                           projected_.end(),
                                           [&source](const auto& entry) { return entry.first == source.id; });
        const bool isNew = previous == projected_.end();

        if (!isNew && previous->second == snapshot)
        {
            stillProjected.emplace_back(source.id, snapshot);
            continue;
        }

        target->setName(toJuce(source.name));

        if (auto* volume = target->getVolumePlugin(); volume != nullptr)
            volume->setVolumeDb(static_cast<float>(source.volumeDb));

        ensureInstrument(*target, source);
        reconcilePlugins(*target, source);

        // Clips are the expensive part, so they are rebuilt only when they
        // actually changed. Dragging a fader touches the volume and nothing else.
        const auto* previousClips = isNew ? nullptr : previous->second.find("clips");
        const auto* currentClips = snapshot.find("clips");
        const bool clipsChanged =
            previousClips == nullptr || currentClips == nullptr || !(*previousClips == *currentClips);

        if (clipsChanged)
            rebuildClips(*target, source);

        stillProjected.emplace_back(source.id, snapshot);
    }

    projected_ = std::move(stillProjected);

    transport_.apply(state_.transport());

    projecting_ = false;
    if (onProjected)
        onProjected();
}

} // namespace daw::engine
