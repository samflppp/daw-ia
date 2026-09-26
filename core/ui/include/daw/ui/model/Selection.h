#pragma once

#include "daw/domain/Ids.h"

#include <juce_events/juce_events.h>

#include <cstddef>

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

    // Which pattern is being worked on. The transport chooses it, the piano
    // roll shows one of its rows, and the beatmaker loop follows it: three
    // screens, one choice, so that switching pattern moves the piano roll and
    // the loop with it.
    //
    // It sits here for the same reason the selected track does: it is not a
    // property of the project. Two windows on the same project would each be
    // looking at their own pattern, and undoing a note must not move the
    // pattern under the user.
    [[nodiscard]] domain::PatternId pattern() const noexcept { return pattern_; }

    // The automation line the user asked to see, and how many times they
    // asked: a right-click on a slider sets it, the playlist scrolls to its
    // lane and lights it, the workspace opens the playlist. Asked twice for
    // the same line is asked twice: the counter moves, so the page comes back
    // to the front even when the line has not changed.
    [[nodiscard]] domain::AutomationLineId automationLine() const noexcept { return automationLine_; }
    [[nodiscard]] std::size_t automationRequests() const noexcept { return automationRequests_; }
    void showAutomation(domain::AutomationLineId line);

    void selectTrack(domain::TrackId track);
    void selectClip(domain::TrackId track, domain::ClipId clip);
    void selectPattern(domain::PatternId pattern);
    void clear();

private:
    domain::TrackId track_{};
    domain::ClipId clip_{};
    domain::PatternId pattern_{};
    domain::AutomationLineId automationLine_{};
    std::size_t automationRequests_{0};
};

} // namespace daw::ui
