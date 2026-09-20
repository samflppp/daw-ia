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

void Selection::clear()
{
    if (track_.isNil() && clip_.isNil())
        return;

    track_ = {};
    clip_ = {};
    sendChangeMessage();
}

} // namespace daw::ui
