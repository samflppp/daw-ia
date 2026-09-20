#include "daw/ui/model/ProjectObserver.h"

namespace daw::ui
{

void ProjectObserver::onExecuted(const domain::Receipt& receipt)
{
    juce::ignoreUnused(receipt);
    sendChangeMessage();
}

void ProjectObserver::onCoalesced(const domain::Receipt& receipt)
{
    juce::ignoreUnused(receipt);
    sendChangeMessage();
}

void ProjectObserver::onUndone(const domain::Receipt& receipt)
{
    juce::ignoreUnused(receipt);
    sendChangeMessage();
}

void ProjectObserver::onRedone(const domain::Receipt& receipt)
{
    juce::ignoreUnused(receipt);
    sendChangeMessage();
}

void ProjectObserver::onHistoryTruncated(std::size_t droppedEntries)
{
    juce::ignoreUnused(droppedEntries);
    sendChangeMessage();
}

} // namespace daw::ui
