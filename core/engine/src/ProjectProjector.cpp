#include "daw/engine/ProjectProjector.h"

#include "HostedParameters.h"
#include "daw/domain/commands/TransportCommands.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

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

// And one level further: a clip carries the key of what it lays down, a
// placement and a row, or the auditioned row in pattern mode. Clips are bound
// by it, so moving one placement moves one clip and rebuilds nothing else.
const juce::Identifier domainClipKeyProperty{"dawDomainClipKey"};

// The sampler a sampler channel plays through carries the digest of the sample
// it holds: a different digest is a different sound, and the only reason to
// touch it.
const juce::Identifier domainSampleProperty{"dawDomainSample"};

// A domain track that holds audio clips is two Tracktion tracks: the one that
// plays its patterns through its instrument, and a companion that plays its
// recordings. An instrument — the 4OSC fallback, a sampler, a VST — replaces
// whatever audio reaches it, so a recording on the instrument's own track
// would be silent. The companion carries the same identifier and this role.
const juce::Identifier domainRoleProperty{"dawDomainRole"};
const juce::String audioRole{"audio"};

// The note that plays a sample at its own pitch. A lit cell plays the channel
// pitch, which is middle C unless the user changed it — so a drum sample
// dropped on the rack sounds as it was recorded.
constexpr int samplerRootNote = 60;

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

bool ProjectProjector::applyTransport(const domain::Receipt& receipt)
{
    // The transport is not project state: it is never serialized, never
    // undone, and the engine moves it on its own. It is therefore driven by
    // the commands themselves and never by a reconciliation, which is also
    // what keeps an edit made while playing from restarting playback.
    if (receipt.type == domain::TransportPlay::commandType)
    {
        // Timed, and the number is written down: starting playback builds the
        // playback graph on this thread, so whatever it costs is time the
        // interface is frozen and the user reads as latency.
        const auto before = juce::Time::getMillisecondCounterHiRes();
        transport_.play();
        const auto cost = juce::Time::getMillisecondCounterHiRes() - before;

        juce::Logger::writeToLog("transport: play() held the message thread for " + juce::String(cost, 1) +
                                 " ms");
        return true;
    }

    if (receipt.type == domain::TransportStop::commandType)
    {
        transport_.stop();

        // The command has already moved the domain's playhead back to the
        // start; the engine follows it rather than deciding on its own.
        transport_.setPosition(state_.transport().positionBeats);
        return true;
    }

    if (receipt.type == domain::TransportSetPosition::commandType)
    {
        transport_.setPosition(state_.transport().positionBeats);
        return true;
    }

    if (receipt.type == domain::TransportSetLoop::commandType)
    {
        reconcileLoop(true);
        return true;
    }

    if (receipt.type == domain::TransportSetMode::commandType)
    {
        // The one transport command that changes what the Edit holds: pattern
        // mode lays the auditioned pattern alone at beat 0, song mode lays the
        // arrangement. Both are what reconcile() already computes from the
        // mode, and the loop comes with it.
        reconcile();
        transport_.setPosition(state_.transport().positionBeats);
        return true;
    }

    return false;
}

void ProjectProjector::onExecuted(const domain::Receipt& receipt)
{
    if (applyTransport(receipt))
        return;

    reconcile();
}

void ProjectProjector::onCoalesced(const domain::Receipt& receipt)
{
    if (applyTransport(receipt))
        return;

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

tracktion::AudioTrack* ProjectProjector::findTrack(const domain::TrackId& id, bool companion) const
{
    const auto wanted = toJuce(id.toString());
    const auto role = companion ? audioRole : juce::String{};

    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track != nullptr && track->state.getProperty(domainTrackIdProperty).toString() == wanted &&
            track->state.getProperty(domainRoleProperty).toString() == role)
            return track;
    }
    return nullptr;
}

tracktion::AudioTrack* ProjectProjector::createTrackFor(const domain::TrackId& id, bool companion)
{
    const auto before = tracktion::getAudioTracks(edit_);
    edit_.ensureNumberOfAudioTracks(before.size() + 1);

    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track != nullptr && !before.contains(track))
        {
            track->state.setProperty(domainTrackIdProperty, toJuce(id.toString()), nullptr);
            if (companion)
                track->state.setProperty(domainRoleProperty, audioRole, nullptr);
            return track;
        }
    }
    return nullptr;
}

void ProjectProjector::applyMix(tracktion::AudioTrack& target, const domain::Track& source)
{
    target.setName(toJuce(source.name));

    if (auto* volume = target.getVolumePlugin(); volume != nullptr)
    {
        volume->setVolumeDb(static_cast<float>(source.volumeDb));

        // The law is written before the position, and on every projection: an
        // Edit built elsewhere, or a Tracktion default moved by another part of
        // the process, would otherwise decide the stereo image of this project.
        volume->setPanLaw(panLaw);
        volume->setPan(static_cast<float>(source.pan));
    }

    // Tracktion's own mute, not a volume of -100 dB: it silences the clips and
    // the instrument that plays them, and it leaves the fader alone, so
    // unmuting gives the track back exactly where the user left it.
    target.setMute(source.muted);
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

    ensureSampler(track, source);

    if (hasDomainInstrument(source) || source.sample.has_value())
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

juce::File ProjectProjector::sampleFile(const domain::SampleRef& sample) const
{
    if (contentStore_ == nullptr)
        return {};

    // The store names its files by digest and nothing else, and an audio
    // reader chooses its format by extension. So the sample is written once
    // more, next to the store, under a name that says what it is. It is a
    // cache: the store stays the truth, and this file can be deleted at will.
    const auto cache = contentStore_->root().getSiblingFile("samples");
    const auto file = cache.getChildFile(toJuce(sample.blob.digest) + "." + toJuce(sample.format));
    if (file.existsAsFile())
        return file;

    auto bytes = contentStore_->get(sample.blob);
    if (!bytes)
        return {};

    static_cast<void>(cache.createDirectory());
    if (!file.replaceWithData(bytes.value().getData(), bytes.value().getSize()))
        return {};

    return file;
}

void ProjectProjector::ensureSampler(tracktion::AudioTrack& track, const domain::Track& source)
{
    const auto samplers = track.pluginList.getPluginsOfType<tracktion::SamplerPlugin>();

    if (!source.sample.has_value())
    {
        for (auto sampler : samplers)
        {
            if (sampler != nullptr && sampler->state.hasProperty(domainSampleProperty))
                sampler->deleteFromParent();
        }
        return;
    }

    const auto wanted = toJuce(source.sample->blob.digest);
    for (auto sampler : samplers)
    {
        if (sampler == nullptr || !sampler->state.hasProperty(domainSampleProperty))
            continue;

        if (sampler->state.getProperty(domainSampleProperty).toString() == wanted)
            return; // already playing this sound

        sampler->deleteFromParent();
    }

    const auto file = sampleFile(*source.sample);
    if (!file.existsAsFile())
        return; // no store or no bytes: silent rather than wrong

    auto plugin = edit_.getPluginCache().createNewPlugin(tracktion::SamplerPlugin::xmlTypeName, {});
    auto* sampler = dynamic_cast<tracktion::SamplerPlugin*>(plugin.get());
    if (sampler == nullptr)
        return;

    sampler->state.setProperty(domainSampleProperty, wanted, nullptr);

    // The whole file, every key, open-ended: a drum hit rings as long as it
    // was recorded, whatever the length of the step that triggered it.
    if (sampler
            ->addSound(file.getFullPathName(), toJuce(source.sample->name), 0.0, source.sample->seconds, 0.0f)
            .isNotEmpty())
        return;

    sampler->setSoundParams(0, samplerRootNote, 0, 127);
    sampler->setSoundOpenEnded(0, true);
    sampler->flushPendingUpdates();

    track.pluginList.insertPlugin(plugin, 0, nullptr);
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

namespace
{

// One clip the Edit has to hold: which row, where, and for how long.
struct LaidOutRow
{
    std::string key;
    double startBeats{0.0};
    double lengthBeats{0.0};
    const domain::Clip* row{nullptr};
};

// Every clip a track plays, in a fixed order, and what depends on the mode.
//
// A pattern says what a track plays and how long the material is; a placement
// says where. Neither alone is a clip on a timeline, and walking them together
// is the only way to get one.
//
//   song     one clip per (placement, row), keyed by both identifiers: the
//            same row laid twice is two clips, and two clips of the same key
//            would be two things nothing could tell apart.
//   pattern  the auditioned pattern alone, at beat 0, keyed apart from every
//            placement so that switching mode never mistakes one for the other.
//
// The order is the patterns' own, then the placements' by beat, so two runs on
// the same state lay the same thing down.
[[nodiscard]] std::vector<LaidOutRow> laidOutRows(const domain::ProjectState& state, domain::TrackId trackId)
{
    std::vector<LaidOutRow> rows;
    const auto& transport = state.transport();

    if (transport.mode == domain::PlayMode::pattern)
    {
        const auto* pattern = state.findPattern(transport.auditionedPattern);
        if (pattern == nullptr)
            return rows;

        if (const auto* row = pattern->findClipForTrack(trackId); row != nullptr)
            rows.push_back(LaidOutRow{"audition:" + row->id.toString(), 0.0, pattern->lengthBeats, row});

        return rows;
    }

    for (const auto& pattern : state.patterns())
    {
        const auto* row = pattern.findClipForTrack(trackId);
        if (row == nullptr)
            continue;

        for (const auto* placement : state.placementsOf(pattern.id))
        {
            rows.push_back(LaidOutRow{placement->id.toString() + ":" + row->id.toString(),
                                      placement->startBeats,
                                      pattern.lengthBeats,
                                      row});
        }
    }

    return rows;
}

// The audio clips a track plays: in song mode only — pattern mode plays the
// auditioned pattern and nothing else of the arrangement.
[[nodiscard]] std::vector<const domain::AudioClip*> laidOutAudio(const domain::ProjectState& state,
                                                                 domain::TrackId trackId)
{
    std::vector<const domain::AudioClip*> clips;
    if (state.transport().mode == domain::PlayMode::pattern)
        return clips;

    for (const auto& clip : state.audioClips())
    {
        if (clip.trackId == trackId)
            clips.push_back(&clip);
    }
    return clips;
}

[[nodiscard]] std::string audioKey(const domain::AudioClip& clip)
{
    return "audio:" + clip.id.toString();
}

[[nodiscard]] domain::Value notesValue(const domain::Clip& row)
{
    domain::Value::Array notes;
    notes.reserve(row.notes.size());
    for (const auto& note : row.notes)
        notes.push_back(note.toValue());
    return domain::Value::array(std::move(notes));
}

void writeNotes(tracktion::MidiClip& clip, const domain::Clip& row)
{
    auto& sequence = clip.getSequence();
    sequence.clear(nullptr);

    // Note positions are relative to the clip, the way Tracktion stores them
    // and the way the domain defines them: a note is written once in the
    // pattern and lands at every placement of it.
    for (const auto& note : row.notes)
    {
        sequence.addNote(note.pitch,
                         tracktion::BeatPosition::fromBeats(note.startBeats),
                         tracktion::BeatDuration::fromBeats(note.lengthBeats),
                         note.velocity,
                         0,
                         nullptr);
    }
}

} // namespace

domain::Value ProjectProjector::playedValue(domain::TrackId trackId) const
{
    domain::Value::Array entries;

    for (const auto& laid : laidOutRows(state_, trackId))
    {
        entries.push_back(domain::Value::object({{"key", domain::Value{laid.key}},
                                                 {"startBeats", domain::Value{laid.startBeats}},
                                                 {"lengthBeats", domain::Value{laid.lengthBeats}},
                                                 {"notes", notesValue(*laid.row)}}));
    }

    for (const auto* clip : laidOutAudio(state_, trackId))
    {
        entries.push_back(domain::Value::object({{"key", domain::Value{audioKey(*clip)}},
                                                 {"startBeats", domain::Value{clip->startBeats}},
                                                 {"seconds", domain::Value{clip->sample.seconds}},
                                                 {"sample", domain::Value{clip->sample.blob.digest}}}));
    }

    return domain::Value::array(std::move(entries));
}

void ProjectProjector::reconcileClips(tracktion::AudioTrack& target, domain::TrackId trackId, bool retimed)
{
    const auto wanted = laidOutRows(state_, trackId);

    const auto isWanted = [&wanted](const juce::String& key)
    {
        return std::any_of(
            wanted.begin(), wanted.end(), [&key](const LaidOutRow& laid) { return toJuce(laid.key) == key; });
    };

    // What the Edit already holds, bound by key and never by position. A clip
    // with no key, or with a key the state no longer lays down, leaves.
    std::vector<std::pair<juce::String, tracktion::MidiClip*>> existing;
    const auto clips = target.getClips();
    for (auto* clip : clips)
    {
        if (clip == nullptr)
            continue;

        const auto key = clip->state.getProperty(domainClipKeyProperty).toString();

        if (auto* midi = dynamic_cast<tracktion::MidiClip*>(clip); midi != nullptr && isWanted(key))
        {
            existing.emplace_back(key, midi);
            continue;
        }

        clip->removeFromParent();
    }

    for (const auto& laid : wanted)
    {
        const auto key = toJuce(laid.key);
        const tracktion::BeatRange beats{tracktion::BeatPosition::fromBeats(laid.startBeats),
                                         tracktion::BeatDuration::fromBeats(laid.lengthBeats)};
        const auto time = edit_.tempoSequence.toTime(beats);

        auto notes = notesValue(*laid.row);

        const auto found = std::find_if(
            existing.begin(), existing.end(), [&key](const auto& entry) { return entry.first == key; });

        auto remembered = std::find_if(projectedClips_.begin(),
                                       projectedClips_.end(),
                                       [&laid](const ProjectedClip& clip) { return clip.key == laid.key; });

        if (found == existing.end() || remembered == projectedClips_.end())
        {
            if (found != existing.end())
                found->second->removeFromParent();

            auto created = target.insertMIDIClip(key, time, nullptr);
            if (created == nullptr)
                continue;

            created->state.setProperty(domainClipKeyProperty, key, nullptr);
            writeNotes(*created, *laid.row);
            ++stats_.clipsInserted;

            if (remembered == projectedClips_.end())
                projectedClips_.push_back(
                    ProjectedClip{laid.key, laid.startBeats, laid.lengthBeats, std::move(notes)});
            else
                *remembered = ProjectedClip{laid.key, laid.startBeats, laid.lengthBeats, std::move(notes)};
            continue;
        }

        auto& clip = *found->second;

        // Where the clip sits is set again whenever its beats moved, and also
        // whenever the tempo did: the Edit holds seconds, and a tempo change
        // moves every second while moving no beat. This is the trap of S7,
        // and repositioning is enough to get out of it — the notes are in
        // beats, relative to the clip, and nothing in them has to change.
        if (retimed || remembered->startBeats != laid.startBeats ||
            remembered->lengthBeats != laid.lengthBeats)
        {
            clip.setPosition(tracktion::ClipPosition{time, {}});
            remembered->startBeats = laid.startBeats;
            remembered->lengthBeats = laid.lengthBeats;
            ++stats_.clipsMoved;
        }

        if (!(remembered->notes == notes))
        {
            writeNotes(clip, *laid.row);
            remembered->notes = std::move(notes);
            ++stats_.clipsRewritten;
        }
    }
}

void ProjectProjector::reconcileAudioTrack(tracktion::AudioTrack& companion,
                                           domain::TrackId trackId,
                                           bool retimed)
{
    const auto audio = laidOutAudio(state_, trackId);

    std::vector<std::pair<juce::String, tracktion::WaveAudioClip*>> existing;
    const auto clips = companion.getClips();
    for (auto* clip : clips)
    {
        if (clip == nullptr)
            continue;

        const auto key = clip->state.getProperty(domainClipKeyProperty).toString();
        const auto wanted =
            std::any_of(audio.begin(),
                        audio.end(),
                        [&key](const domain::AudioClip* laid) { return toJuce(audioKey(*laid)) == key; });

        if (auto* wave = dynamic_cast<tracktion::WaveAudioClip*>(clip); wave != nullptr && wanted)
        {
            existing.emplace_back(key, wave);
            continue;
        }

        clip->removeFromParent();
    }

    reconcileAudio(companion, audio, existing, retimed);
}

void ProjectProjector::reconcileAudio(
    tracktion::AudioTrack& target,
    const std::vector<const domain::AudioClip*>& wanted,
    std::vector<std::pair<juce::String, tracktion::WaveAudioClip*>>& existing,
    bool retimed)
{
    for (const auto* clip : wanted)
    {
        const auto key = toJuce(audioKey(*clip));

        // In beats where it starts, in seconds how long it lasts: a recording
        // is not stretched by a tempo change, it is only moved.
        const auto start = edit_.tempoSequence.toTime(tracktion::BeatPosition::fromBeats(clip->startBeats));
        const tracktion::TimeRange time{start, tracktion::TimeDuration::fromSeconds(clip->sample.seconds)};

        const auto found = std::find_if(
            existing.begin(), existing.end(), [&key](const auto& entry) { return entry.first == key; });

        auto remembered =
            std::find_if(projectedClips_.begin(),
                         projectedClips_.end(),
                         [clip](const ProjectedClip& projected) { return projected.key == audioKey(*clip); });

        if (found == existing.end())
        {
            const auto file = sampleFile(clip->sample);
            if (!file.existsAsFile())
                continue;

            auto created = target.insertWaveClip(
                toJuce(clip->sample.name), file, tracktion::ClipPosition{time, {}}, false);
            if (created == nullptr)
                continue;

            created->state.setProperty(domainClipKeyProperty, key, nullptr);
            ++stats_.clipsInserted;

            ProjectedClip projected{audioKey(*clip), clip->startBeats, clip->sample.seconds, {}};
            if (remembered == projectedClips_.end())
                projectedClips_.push_back(std::move(projected));
            else
                *remembered = std::move(projected);
            continue;
        }

        if (remembered == projectedClips_.end() || retimed || remembered->startBeats != clip->startBeats)
        {
            found->second->setPosition(tracktion::ClipPosition{time, {}});
            ++stats_.clipsMoved;

            if (remembered == projectedClips_.end())
                projectedClips_.push_back(
                    ProjectedClip{audioKey(*clip), clip->startBeats, clip->sample.seconds, {}});
            else
                remembered->startBeats = clip->startBeats;
        }
    }
}

void ProjectProjector::forgetClipsNotLaidOut()
{
    std::vector<std::string> keys;
    for (const auto& source : state_.tracks())
    {
        for (const auto& laid : laidOutRows(state_, source.id))
            keys.push_back(laid.key);
        for (const auto* clip : laidOutAudio(state_, source.id))
            keys.push_back(audioKey(*clip));
    }

    projectedClips_.erase(
        std::remove_if(projectedClips_.begin(),
                       projectedClips_.end(),
                       [&keys](const ProjectedClip& clip)
                       { return std::find(keys.begin(), keys.end(), clip.key) == keys.end(); }),
        projectedClips_.end());
}

void ProjectProjector::reconcileLoop(bool force)
{
    const auto& transport = state_.transport();

    bool looping = transport.looping;
    double start = transport.loopStartBeats;
    double end = transport.loopEndBeats;

    // Pattern mode loops over the auditioned pattern, and over nothing else:
    // its length is read here, at every projection, so lengthening the pattern
    // lengthens the loop without a transport command of its own.
    if (transport.mode == domain::PlayMode::pattern)
    {
        const auto* pattern = state_.findPattern(transport.auditionedPattern);
        looping = pattern != nullptr;
        start = 0.0;
        end = pattern != nullptr ? pattern->lengthBeats : 0.0;
    }

    auto wanted = domain::Value::object(
        {{"looping", domain::Value{looping}}, {"start", domain::Value{start}}, {"end", domain::Value{end}}});

    if (!force && wanted == projectedLoop_)
        return;

    transport_.setLoop(looping, start, end);
    projectedLoop_ = std::move(wanted);
}

bool ProjectProjector::reconcileTempo()
{
    domain::Value::Array snapshot;
    snapshot.reserve(state_.tempoPoints().size());
    for (const auto& point : state_.tempoPoints())
        snapshot.push_back(point.toValue());

    auto wanted = domain::Value::array(std::move(snapshot));
    if (wanted == projectedTempo_)
        return false;

    auto& sequence = edit_.tempoSequence;

    // Rebuilt whole rather than diffed. A tempo sequence holds a handful of
    // points, and matching them one by one would mean binding by index — the
    // very thing the track and plugin projections refuse to do.
    while (sequence.getNumTempos() > 1)
        sequence.removeTempo(sequence.getNumTempos() - 1, false);

    const auto& points = state_.tempoPoints();

    // The first Tracktion tempo is the one a new Edit already has, and it sits
    // at beat 0 like the domain's own origin point. They are the same object,
    // so it is set and never inserted.
    if (auto* first = sequence.getTempo(0); first != nullptr)
        first->setBpm(points.front().beatsPerMinute);

    for (std::size_t index = 1; index < points.size(); ++index)
    {
        // remapEdit is false throughout, and insertTempo takes beats: the
        // domain anchors its points in beats, so a tempo change moves no
        // musical content. Nothing to remap, by construction.
        sequence.insertTempo(
            tracktion::BeatPosition::fromBeats(points[index].startBeats), points[index].beatsPerMinute, 0.0f);
    }

    projectedTempo_ = std::move(wanted);
    return true;
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

    // Every clip was placed in seconds computed from the old sequence. They
    // are all set again below, and none of them is rebuilt: a tempo change
    // moves seconds, never beats, and the notes are in beats.
    const bool retimed = reconcileTempo();

    removeUnknownTracks();

    std::vector<std::pair<domain::TrackId, domain::Value>> stillProjected;
    stillProjected.reserve(state_.tracks().size());

    for (const auto& source : state_.tracks())
    {
        auto* target = findTrack(source.id, false);
        if (target == nullptr)
            target = createTrackFor(source.id, false);
        if (target == nullptr)
            continue;

        // The track's own form, and what it plays. The second is not a
        // property of the track any more — the notes live in the patterns —
        // so a snapshot made of the track alone would miss every edit made to
        // a pattern and leave the Edit silent.
        const auto snapshot =
            domain::Value::object({{"track", source.toValue()}, {"played", playedValue(source.id)}});

        const auto previous = std::find_if(projected_.begin(),
                                           projected_.end(),
                                           [&source](const auto& entry) { return entry.first == source.id; });
        const bool isNew = previous == projected_.end();

        const auto partChanged = [&](std::string_view part)
        {
            const auto* before = isNew ? nullptr : previous->second.find(part);
            const auto* now = snapshot.find(part);
            return before == nullptr || now == nullptr || !(*before == *now);
        };

        const bool trackChanged = partChanged("track");
        const bool playedChanged = partChanged("played");

        if (trackChanged)
        {
            applyMix(*target, source);
            ensureInstrument(*target, source);
            reconcilePlugins(*target, source);
        }

        // Clips are the expensive part, so they are looked at only when what
        // the track plays changed, or when the tempo moved the seconds under
        // them. Even then each one is bound by its key and touched only in
        // what changed: moving one placement repositions one clip, and adding
        // a note to a pattern laid eight times rewrites eight sequences and
        // inserts nothing.
        if (playedChanged || retimed)
            reconcileClips(*target, source.id, retimed);

        // The recordings, on the companion: made the first time the track has
        // one, mixed like the track itself, never given an instrument.
        auto* companion = findTrack(source.id, true);
        const bool needsCompanion = companion == nullptr && !laidOutAudio(state_, source.id).empty();
        if (needsCompanion)
            companion = createTrackFor(source.id, true);

        if (companion != nullptr)
        {
            if (trackChanged || needsCompanion)
                applyMix(*companion, source);
            if (playedChanged || retimed || needsCompanion)
                reconcileAudioTrack(*companion, source.id, retimed);
        }

        stillProjected.emplace_back(source.id, snapshot);
    }

    projected_ = std::move(stillProjected);
    forgetClipsNotLaidOut();
    reconcileLoop(retimed);

    projecting_ = false;
    if (onProjected)
        onProjected();
}

} // namespace daw::engine
