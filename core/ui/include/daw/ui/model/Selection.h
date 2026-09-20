#pragma once

#include "daw/domain/Ids.h"

#include <juce_events/juce_events.h>

namespace daw::ui
{

// What the user is looking at. Deliberately not in ProjectState.
//
// A selection is not a property of the project: it is not journalled, it is not
// undone, and two windows on the same project would each have their own. Put in
// ProjectState it would need a command, and undoing a note would then also
// undo a click.
class Selection final : public juce::ChangeBroadcaster
{
public:
    [[nodiscard]] domain::TrackId track() const noexcept { return track_; }
    [[nodiscard]] domain::ClipId clip() const noexcept { return clip_; }

    void selectTrack(domain::TrackId track);
    void selectClip(domain::TrackId track, domain::ClipId clip);
    void clear();

private:
    domain::TrackId track_{};
    domain::ClipId clip_{};
};

} // namespace daw::ui
