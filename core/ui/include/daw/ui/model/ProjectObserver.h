#pragma once

#include "daw/domain/command/BusObserver.h"

#include <juce_events/juce_events.h>

namespace daw::ui
{

// Turns what the bus reports into one repaint.
//
// Two reasons it is not "every panel observes the bus". First, an observer must
// not call back into the bus, and a panel that repaints from inside the
// notification is one careless line away from doing exactly that. Second, a
// fader sweep emits sixty notifications in a second; sixty full repaints of the
// screen for a gesture the user sees as one movement is how an interface starts
// feeling heavy.
//
// juce::ChangeBroadcaster answers both: it defers to the message thread and it
// collapses the burst into a single call.
class ProjectObserver final : public domain::BusObserver, public juce::ChangeBroadcaster
{
public:
    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;
    void onHistoryTruncated(std::size_t droppedEntries) override;
};

} // namespace daw::ui
