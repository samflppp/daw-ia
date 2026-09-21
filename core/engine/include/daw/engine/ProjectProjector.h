#pragma once

#include "daw/domain/Value.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/ContentStore.h"
#include "daw/engine/PluginCatalogue.h"
#include "daw/engine/TransportController.h"

#include <tracktion_engine/tracktion_engine.h>

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
// is skipped entirely, and its clips are rebuilt only when the clips changed.
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

    [[nodiscard]] tracktion::AudioTrack* findTrack(const domain::TrackId& id) const;
    [[nodiscard]] tracktion::AudioTrack* createTrackFor(const domain::TrackId& id);
    void removeUnknownTracks();
    void ensureInstrument(tracktion::AudioTrack& track, const domain::Track& source);
    void rebuildClips(tracktion::AudioTrack& target, const domain::Track& source);

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

    // Last projected tempo sequence. Rebuilding it costs little, but rebuilding
    // it for nothing would drag every clip of the Edit with it.
    domain::Value projectedTempo_;
};

} // namespace daw::engine
