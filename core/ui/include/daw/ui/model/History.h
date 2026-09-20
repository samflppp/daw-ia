#pragma once

#include "daw/ui/history/HistoryLog.h"

#include <juce_events/juce_events.h>

namespace daw::ui
{

// The history log, plus the one thing JUCE adds to it: a repaint.
//
// The split is the same one the whole interface is built on. What the list
// contains is arithmetic and lives in HistoryLog, where a test reaches it
// without a message thread. Telling the screen about it is a framework
// concern and stops here.
class History final : public HistoryLog, public juce::ChangeBroadcaster
{
public:
    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;
    void onHistoryTruncated(std::size_t droppedEntries) override;
};

} // namespace daw::ui
