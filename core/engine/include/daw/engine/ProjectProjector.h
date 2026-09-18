#pragma once

#include "daw/domain/Value.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/TransportController.h"

#include <tracktion_engine/tracktion_engine.h>

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
    ProjectProjector(tracktion::Edit& edit, const domain::ProjectState& state);

    // Idempotent: calling it twice in a row changes nothing the second time.
    void reconcile();

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;

private:
    [[nodiscard]] tracktion::AudioTrack* findTrack(const domain::TrackId& id) const;
    [[nodiscard]] tracktion::AudioTrack* createTrackFor(const domain::TrackId& id);
    void removeUnknownTracks();
    static void ensureInstrument(tracktion::AudioTrack& track, tracktion::Edit& edit);
    void rebuildClips(tracktion::AudioTrack& target, const domain::Track& source);

    tracktion::Edit& edit_;
    const domain::ProjectState& state_;
    TransportController transport_;

    // Last projected form, keyed by domain identifier. Lets an unchanged track
    // be skipped without ever binding by position.
    std::vector<std::pair<domain::TrackId, domain::Value>> projected_;
};

} // namespace daw::engine
