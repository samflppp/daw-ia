#pragma once

#include "daw/domain/Value.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/PluginCatalogue.h"
#include "daw/engine/TransportController.h"

#include <tracktion_engine/tracktion_engine.h>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace daw::engine
{

// Projects ProjectState onto a Tracktion Edit.
//
// Reconciliation, not translation. The projector observes the bus, but the
// notification is only a *trigger*: it then reads the whole state and brings
// the Edit into agreement. Undo, redo, coalescing and history truncation all
// produce nothing more than "a new state", so none of them needs a single line
// of code here. A command added later needs none either, as long as it only
// touches entities this class already projects.
//
// The price is that reconciling is more work than one direct call. It is paid
// on purpose, and it is bounded: a track whose serialized form has not changed
// is skipped entirely, and what it plays is rebuilt only when that changed.
//
// Binding is by identity, never by position: each Tracktion track carries the
// domain TrackId in its state tree, so reordering or deleting a track in the
// middle cannot make the projector write into the wrong one.
class ProjectProjector final : public domain::BusObserver
{
public:
    // The pan law: how a position between -1 and +1 becomes a left gain and a
    // right gain. The domain carries the position and never the gains, so this
    // is the one place the question is answered.
    //
    // It is one of Tracktion's five, not one invented here. It is *not* the one
    // Tracktion returns by default, and that is deliberate, twice over:
    //
    //   - getDefaultPanLaw() is a mutable global of the process. A project
    //     whose stereo image depended on it would not render the same on two
    //     machines, for the same reason a chain bound by index would not
    //     reload the same. The law is therefore written onto every volume
    //     plugin explicitly, never left at PanLawDefault.
    //
    //   - that factory default is PanLawLinear, which computes
    //     L = g - pan*g and R = g + pan*g: hard right gives R = 2g, a track
    //     made 6 dB louder by being panned. PanLaw3dBCenter is constant power:
    //     -3 dB at the centre, unity at either extreme, and equal loudness all
    //     the way across.
    static constexpr tracktion::PanLaw panLaw = tracktion::PanLaw3dBCenter;

    // The catalogue resolves a PluginRef into an installed plugin, and the store
    // holds the opaque states. Both are optional: a projection without them
    // still does tracks, clips, notes and volume, which is all a test that
    // touches no plugin needs.
    ProjectProjector(tracktion::Edit& edit,
                     const domain::ProjectState& state,
                     PluginCatalogue* catalogue = nullptr,
                     ContentStore* contentStore = nullptr);

    // True while reconcile() is writing into the Edit.
    //
    // The parameter bridge reads it and stays silent: projecting a value makes
    // the plugin report that value back, and turning that echo into a command
    // would fight the user's own movement — or undo it.
    [[nodiscard]] bool isProjecting() const noexcept { return projecting_; }

    // Called at the end of every reconcile(), once the Edit agrees with the
    // state and the projecting flag is down.
    //
    // The parameter bridge hangs on it instead of observing the bus: a bus
    // observer would have to be registered after the projector to see a plugin
    // that the projection has just created, and an ordering rule nobody can see
    // in the code is a rule that breaks. Here the order is causality.
    std::function<void()> onProjected;

    // Plugins the project names but this machine does not have. Reported rather
    // than guessed: loading another plugin in its place would silently change
    // the sound of a project.
    [[nodiscard]] const std::vector<std::string>& missingPlugins() const noexcept { return missing_; }

    // Idempotent: calling it twice in a row changes nothing the second time.
    void reconcile();

    // What the clip reconciliation did since the projector was built. Read by
    // the engine tests, which have to prove that an unchanged clip is not
    // rebuilt; nothing else depends on it.
    struct Stats
    {
        std::size_t clipsInserted{0};
        std::size_t clipsMoved{0};
        std::size_t clipsRewritten{0};
    };

    [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;

private:
    // Brings the Edit's tempo sequence into agreement with the domain's, and
    // says whether anything moved. It did, every clip has to be laid out
    // again: a clip is inserted into the Edit as a time range, and the beats
    // it came from now map onto other seconds.
    // Carries out a transport command, and says whether the receipt was one.
    // Anything else is a change to the project, and goes through reconcile().
    [[nodiscard]] bool applyTransport(const domain::Receipt& receipt);

    [[nodiscard]] bool reconcileTempo();

    // The Tracktion track of a domain track: the one that plays its patterns,
    // or, with `companion`, the one that plays its audio clips.
    [[nodiscard]] tracktion::AudioTrack* findTrack(const domain::TrackId& id, bool companion) const;
    [[nodiscard]] tracktion::AudioTrack* createTrackFor(const domain::TrackId& id, bool companion);

    // Name, volume, pan law, pan and mute: written the same on a track and on
    // its companion, so a recording and a pattern of one track mix as one.
    void applyMix(tracktion::AudioTrack& target, const domain::Track& source);

    // The audio clips of a track, on its companion.
    void reconcileAudioTrack(tracktion::AudioTrack& companion, domain::TrackId trackId, bool retimed);
    void removeUnknownTracks();

    // The level tap of a chain, last in it, measuring `strip`: a domain
    // TrackId, or the master. Placed when missing, put back at the end when a
    // plugin landed after it, never duplicated. `audible` is what the
    // projection decided about the strip — see MeterTapPlugin::setAudible.
    void ensureMeterTap(tracktion::PluginList& list, const juce::String& strip, bool audible);

    // The master leaves the Edit at unity. Tracktion's own master fader does
    // not start at 0 dB, and nothing in the domain asked it to be anything
    // else: a mix 3 dB quieter than its meters, measured at S11, is the
    // difference.
    void reconcileMaster();
    void ensureInstrument(tracktion::AudioTrack& track, const domain::Track& source);

    // Brings the clips of one track into agreement with what it plays.
    //
    // One Tracktion clip per (placement, pattern row) pair in song mode, one per
    // row of the auditioned pattern in pattern mode. This is the one place where
    // content and position meet, and it is a projection and not a state: the
    // domain holds the notes once, in the pattern, whatever the number of
    // placements.
    //
    // Bound by key, never by position. A clip whose key is gone leaves, a key
    // with no clip gets one, and a clip that stays is touched only in what
    // changed: its position when its beats or the tempo moved, its notes when
    // they did. `retimed` says the tempo moved and every clip has to be set
    // again in seconds.
    void reconcileClips(tracktion::AudioTrack& target, domain::TrackId trackId, bool retimed);

    // The audio clips of one track, bound by key like the pattern rows: laid
    // when new, moved when their beat or the tempo moved, never rebuilt.
    void reconcileAudio(tracktion::AudioTrack& target,
                        const std::vector<const domain::AudioClip*>& wanted,
                        std::vector<std::pair<juce::String, tracktion::WaveAudioClip*>>& existing,
                        bool retimed);

    // A sampler channel plays its sample through a Tracktion sampler that this
    // class owns, marked with the sample's digest. Replaced only when the
    // digest changes; removed when the track stops being a sampler channel.
    void ensureSampler(tracktion::AudioTrack& track, const domain::Track& source);

    // A readable copy of a sample, with the extension its format needs. Empty
    // when there is no store or the store has lost the bytes.
    [[nodiscard]] juce::File sampleFile(const domain::SampleRef& sample) const;

    // Drops the memory of clips nothing lays down any more, across every track.
    void forgetClipsNotLaidOut();

    // The loop the engine has to run: the auditioned pattern's length in
    // pattern mode, the one transport.set_loop asked for in song mode. Applied
    // when it changed, or when `force` says the seconds under it moved.
    void reconcileLoop(bool force);

    // The serialized form of what that track plays. Compared against the last
    // projection to decide whether its clips have to be looked at: a fader drag
    // must not touch them, and a note added to a pattern must.
    [[nodiscard]] domain::Value playedValue(domain::TrackId trackId) const;

    // --- plugins
    void reconcilePlugins(tracktion::AudioTrack& target, const domain::Track& source);
    [[nodiscard]] static tracktion::Plugin* findPlugin(tracktion::AudioTrack& track,
                                                       const domain::PluginId& id);
    [[nodiscard]] tracktion::Plugin::Ptr createPluginFor(const domain::PluginInstance& source);
    static void removeUnknownPlugins(tracktion::AudioTrack& track, const domain::Track& source);
    void applyPluginState(tracktion::Plugin& target, const domain::PluginInstance& source);
    static void applyPluginParameters(tracktion::Plugin& target, const domain::PluginInstance& source);
    [[nodiscard]] bool isInstrument(const domain::PluginRef& ref) const;
    [[nodiscard]] bool hasDomainInstrument(const domain::Track& source) const;

    tracktion::Edit& edit_;
    const domain::ProjectState& state_;
    PluginCatalogue* catalogue_{nullptr};
    ContentStore* contentStore_{nullptr};
    TransportController transport_;
    bool projecting_{false};
    std::vector<std::string> missing_;

    // The state digest last written into each plugin instance. A plugin whose
    // digest has not changed is left alone: pushing a blob back into a running
    // plugin would throw away whatever the user just did in its own window.
    std::vector<std::pair<domain::PluginId, std::string>> projectedStates_;

    // Last projected form, keyed by domain identifier. Lets an unchanged track
    // be skipped without ever binding by position.
    std::vector<std::pair<domain::TrackId, domain::Value>> projected_;

    // Last projected form of each clip, keyed like the clip itself.
    struct ProjectedClip
    {
        std::string key;
        double startBeats{0.0};
        double lengthBeats{0.0};
        domain::Value notes;
    };

    std::vector<ProjectedClip> projectedClips_;

    domain::Value projectedLoop_;
    Stats stats_;

    // Last projected tempo sequence. Rebuilding it costs little, but rebuilding
    // it for nothing would drag every clip of the Edit with it.
    domain::Value projectedTempo_;
};

} // namespace daw::engine
