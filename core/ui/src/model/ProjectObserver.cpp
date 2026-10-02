#include "daw/ui/model/ProjectObserver.h"

namespace daw::ui
{

void ProjectObserver::onExecuted(const domain::Receipt& receipt)
{
    changed(receipt.reach);
}

void ProjectObserver::onCoalesced(const domain::Receipt& receipt)
{
    changed(receipt.reach);
}

void ProjectObserver::onUndone(const domain::Receipt& receipt)
{
    changed(receipt.reach);
}

void ProjectObserver::onRedone(const domain::Receipt& receipt)
{
    changed(receipt.reach);
}

void ProjectObserver::onHistoryTruncated(std::size_t droppedEntries)
{
    juce::ignoreUnused(droppedEntries);
    changed(domain::Reach::anything);
}

void ProjectObserver::changed(domain::Reach reach)
{
    ++revision_;

    // What the command declares, not what it is called (S19).
    if (reach != domain::Reach::notes)
        lastBeyondNotes_ = revision_;

    sendChangeMessage();
}

} // namespace daw::ui
