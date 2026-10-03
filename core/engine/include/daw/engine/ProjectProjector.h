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
#include <optional>
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
    // The Tracktion tracks a domain track is made of (S21): its strip, and
    // the two that play into it — its notes and its recordings.
    enum class Part
    {
        strip,
        notes,
        recordings
    };

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

    // The law of a bus and of the master: a balance, unity at the centre.
    // Tracktion has no true balance law; the linear one is unity at the centre
    // and doubles one side at either extreme (+6 dB). A bus is rarely panned
    // hard, and the limit is written down in the S11 review.
    static constexpr tracktion::PanLaw busPanLaw = tracktion::PanLawLinear;

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

    // --- listening before writing (S16)
    //
    // A proposal heard in a loop, without entering the project or its history.
    // ProjectState stays the one truth: nothing here reads back into it, and
    // the transport commands are not used, because they would change its
    // transport. What changes is only the Edit, which was always a projection:
    // while a row is listened to, its clips are laid with the notes of the
    // range replaced by the proposed ones -- exactly what Tab would write -- and
    // the engine loops over the range.
    //
    // Nothing new runs in the audio callback. The clips are rewritten here, on
    // the message thread, the way every edit of a note has reached the engine
    // since S3, and Tracktion rebuilds its playback graph off the audio thread.
    //
    // Any transport command ends the listening first: the person asked for
    // the project, not for the proposal.
    struct Audition
    {
        domain::TrackId track;
        domain::PatternId pattern;
        double fromBeats{0.0}; // in the pattern
        double toBeats{0.0};
        std::vector<domain::Note> notes; // in the pattern, the range's notes replaced by these

        // A pattern the proposal would create (S17, the multi-track zone):
        // not in the project, so it has no placement to be heard at. It is
        // laid here, in song beats, for as long as it lasts, and leaves with
        // the listening. Ignored for a pattern that exists.
        std::optional<double> newAtBeats{};
        double newLengthBeats{0.0};
    };

    // Starts, or changes what is heard. The loop is in the Edit's beats. The
    // playhead goes to its start when the engine was stopped or the loop moved;
    // another variant of the same range keeps playing where it is.
    void listen(std::vector<Audition> auditions, double loopStartBeats, double loopEndBeats);

    // Back to the project: its clips, its loop, and its transport -- stopped
    // where the domain says unless the domain says it plays.
    void stopListening();

    [[nodiscard]] bool listening() const noexcept { return listenLoop_.has_value(); }

    // Called when a listening ends on its own: a transport command.
    std::function<void()> onListeningEnded;

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

    // A Tracktion track of a domain track (S21): its strip — inserts, fader,
    // sends, meter —, or the track that plays its notes, or the one that
    // plays its recordings, both into the strip. See the role in the source.
    [[nodiscard]] tracktion::AudioTrack* findTrack(const domain::TrackId& id, Part part) const;
    [[nodiscard]] tracktion::AudioTrack* createTrackFor(const domain::TrackId& id, Part part);

    // The track's plugins up to its first instrument, played on the notes'
    // track, and the rest, inserts of the strip.
    [[nodiscard]] std::pair<domain::Track, domain::Track>
    splitAtInstrument(const domain::Track& source) const;

    // A part plays into its strip, never to the master.
    static void playInto(tracktion::AudioTrack& part, tracktion::AudioTrack& strip);

    // Name, volume, pan law and pan, on the strip.
    void applyMix(tracktion::AudioTrack& target, const domain::Track& source);

    // Where the strip goes and what it is heard as: its mute, which is the
    // domain's isAudible and not its own muted flag — a solo elsewhere can
    // silence it —, its output, bus or master, and its sends, one AuxSend per
    // bus, after the fader. Written on the strip.
    void applyRoute(tracktion::AudioTrack& target, const domain::Track& source);

    // What applyRoute depends on, beyond the strip itself: whether it is
    // heard, and the aux number of every bus it reaches. A solo on another
    // track, or a bus removed before this one, changes it.
    [[nodiscard]] domain::Value routeValue(const domain::Track& source) const;

    // The aux number of a bus: its rank among the buses, 0 to 31.
    [[nodiscard]] int busNumber(const domain::TrackId& bus) const;

    // A bus: a Tracktion track with no clip, an AuxReturn in front of its
    // chain, then its inserts, its fader and its tap.
    void reconcileBus(const domain::Track& bus,
                      std::vector<std::pair<domain::TrackId, domain::Value>>& projected);
    void ensureAuxReturn(tracktion::AudioTrack& track, int number);

    // The audio clips of a track, on the track of its recordings.
    void reconcileAudioTrack(tracktion::AudioTrack& recordings, domain::TrackId trackId, bool retimed);
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

    // --- automation
    //
    // Each line of the domain becomes the AutomationCurve of the Tracktion
    // parameters it drives: a channel's volume or pan on its strip, a bus's,
    // the master fader's, or one parameter of a plugin.
    //
    // Tracktion's curve is in seconds and the domain's line in beats, so the
    // curve is computed again whenever the tempo moves; a segment that crosses
    // a tempo change is cut there, so it keeps its shape in beats. The volume
    // goes to Tracktion as a fader position, the space the curve bends in.
    //
    // Pattern mode plays no automation: a line is in the arrangement, and the
    // arrangement is silent in pattern mode. A muted master keeps its fader at
    // the floor, whatever its line says.
    //
    // Compared against what the curve already holds, so a projection that
    // changes nothing about automation writes nothing.
    void reconcileAutomation();
    [[nodiscard]] std::vector<tracktion::AutomatableParameter*>
    parametersOf(const domain::AutomationTarget& target);
    [[nodiscard]] tracktion::VolumeAndPanPlugin* masterFader() const;

    // --- plugins
    //
    // A chain is a PluginList, on a track or on the master. The domain's
    // inserts go after `offset` plugins of the projection's own that lead the
    // chain: the fallback synth, a sampler, an AuxReturn.
    void reconcilePlugins(tracktion::PluginList& list, const domain::Track& source, int offset);
    [[nodiscard]] static tracktion::Plugin* findPlugin(tracktion::PluginList& list,
                                                       const domain::PluginId& id);
    [[nodiscard]] tracktion::Plugin::Ptr createPluginFor(const domain::PluginInstance& source);
    static void removeUnknownPlugins(tracktion::PluginList& list, const domain::Track& source);

    // An effect of the DAW (S20) is one or two of Tracktion's own: the
    // equaliser is a high-pass followed by the 4-band equaliser, both marked
    // with the domain identifier and told apart by their part. Its parameters
    // are written in the units the domain holds them in.
    [[nodiscard]] static std::vector<tracktion::Plugin*> partsOf(tracktion::PluginList& list,
                                                                 const domain::PluginId& id);
    [[nodiscard]] std::vector<tracktion::Plugin::Ptr> createInternal(const domain::PluginInstance& source);
    static void applyInternal(const std::vector<tracktion::Plugin*>& parts,
                              const domain::PluginInstance& source);
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

    std::vector<Audition> auditions_;
    std::optional<std::pair<double, double>> listenLoop_;

    // The master's last projected form, like a track's.
    domain::Value projectedMaster_;

    // The parameters a curve was written on, so a line that goes away leaves
    // its parameter at its static value instead of on a curve nothing owns.
    std::vector<tracktion::AutomatableParameter::Ptr> automated_;

    // Last projected tempo sequence. Rebuilding it costs little, but rebuilding
    // it for nothing would drag every clip of the Edit with it.
    domain::Value projectedTempo_;
};

} // namespace daw::engine
