#pragma once

#include "daw/domain/live/Router.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/ProjectObserver.h"
#include "daw/ui/model/Selection.h"

#include <juce_events/juce_events.h>

#include <string>

namespace daw::app
{

// Playing live, on the application's side (S23): which track the keys play.
// The one chosen in the rack, the selection's track. None chosen, or the
// chosen one removed: none, and a key plays nothing — the screen says so.
// Followed on the message thread; the router does the rest without it.
class LivePlay final : private juce::ChangeListener
{
public:
    LivePlay(domain::live::Router& router,
             const domain::ProjectState& state,
             ui::Selection& selection,
             ui::ProjectObserver& project);
    ~LivePlay() override;

    LivePlay(const LivePlay&) = delete;
    LivePlay& operator=(const LivePlay&) = delete;
    LivePlay(LivePlay&&) = delete;
    LivePlay& operator=(LivePlay&&) = delete;

    // The track the keys play now, empty when none.
    [[nodiscard]] std::string target() const { return router_.target(); }

    // Reads the selection and the project again. Called on every change of
    // either; a test calls it to act at once.
    void follow();

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    domain::live::Router& router_;
    const domain::ProjectState& state_;
    ui::Selection& selection_;
    ui::ProjectObserver& project_;
};

} // namespace daw::app
