#include "daw/ui/model/Selection.h"

namespace daw::ui
{

void Selection::selectTrack(domain::TrackId track)
{
    if (track_ == track && clip_.isNil())
        return;

    track_ = track;
    clip_ = {};
    sendChangeMessage();
}

void Selection::selectClip(domain::TrackId track, domain::ClipId clip)
{
    if (track_ == track && clip_ == clip)
        return;

    track_ = track;
    clip_ = clip;
    sendChangeMessage();
}

void Selection::selectPattern(domain::PatternId pattern)
{
    if (pattern_ == pattern)
        return;

    // The row goes with it: a clip of the pattern that was showing names
    // nothing in the one that is. The track stays — a beatmaker changes
    // pattern to keep working on the same channel.
    pattern_ = pattern;
    clip_ = {};
    sendChangeMessage();
}

void Selection::showAutomation(domain::AutomationLineId line)
{
    automationLine_ = line;
    ++automationRequests_;
    sendChangeMessage();
}

void Selection::clear()
{
    if (track_.isNil() && clip_.isNil() && pattern_.isNil())
        return;

    track_ = {};
    clip_ = {};
    pattern_ = {};
    sendChangeMessage();
}

} // namespace daw::ui
