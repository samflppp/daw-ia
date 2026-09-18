#include "daw/engine/ProjectProjector.h"

#include <algorithm>

namespace daw::engine
{
namespace
{

// The domain identifier, carried inside the Tracktion track state. This is the
// binding between the two worlds, and it is the only thing the Edit knows
// about the domain.
const juce::Identifier domainTrackIdProperty{"dawDomainTrackId"};

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str(), static_cast<int>(text.size()));
}

} // namespace

ProjectProjector::ProjectProjector(tracktion::Edit& edit, const domain::ProjectState& state)
    : edit_{edit}
    , state_{state}
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

void ProjectProjector::ensureInstrument(tracktion::AudioTrack& track, tracktion::Edit& edit)
{
    // Without an instrument a MIDI clip is silent. 4OSC ships with Tracktion,
    // so the project makes a sound without any external plugin. Hosting VST3
    // and CLAP is a later week.
    if (!track.pluginList.getPluginsOfType<tracktion::FourOscPlugin>().isEmpty())
        return;

    if (auto plugin = edit.getPluginCache().createNewPlugin(tracktion::FourOscPlugin::xmlTypeName, {});
        plugin != nullptr)
    {
        track.pluginList.insertPlugin(plugin, 0, nullptr);
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

        ensureInstrument(*target, edit_);

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
}

} // namespace daw::engine
