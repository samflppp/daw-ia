#include "LivePlay.h"

namespace daw::app
{

LivePlay::LivePlay(domain::live::Router& router,
                   const domain::ProjectState& state,
                   ui::Selection& selection,
                   ui::ProjectObserver& project)
    : router_(router)
    , state_(state)
    , selection_(selection)
    , project_(project)
{
    selection_.addChangeListener(this);
    project_.addChangeListener(this);
    follow();
}

LivePlay::~LivePlay()
{
    project_.removeChangeListener(this);
    selection_.removeChangeListener(this);
    static_cast<void>(router_.releaseAll(domain::live::now()));
}

void LivePlay::follow()
{
    const auto chosen = selection_.track();
    const bool exists = !chosen.isNil() && state_.findTrack(chosen) != nullptr;
    const auto wanted = exists ? chosen.toString() : std::string{};
    if (wanted != router_.target())
        router_.setTarget(wanted);
}

void LivePlay::changeListenerCallback(juce::ChangeBroadcaster*)
{
    follow();
}

} // namespace daw::app
