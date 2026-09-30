#include "daw/ui/model/ProjectObserver.h"

namespace daw::ui
{

void ProjectObserver::onExecuted(const domain::Receipt& receipt)
{
    changed(receipt.type);
}

void ProjectObserver::onCoalesced(const domain::Receipt& receipt)
{
    changed(receipt.type);
}

void ProjectObserver::onUndone(const domain::Receipt& receipt)
{
    changed(receipt.type);
}

void ProjectObserver::onRedone(const domain::Receipt& receipt)
{
    changed(receipt.type);
}

void ProjectObserver::onHistoryTruncated(std::size_t droppedEntries)
{
    juce::ignoreUnused(droppedEntries);
    changed({});
}

void ProjectObserver::changed(std::string_view type)
{
    ++revision_;

    // The note commands change the notes of a clip and nothing else.
    if (!type.starts_with("note."))
        lastBeyondNotes_ = revision_;

    sendChangeMessage();
}

} // namespace daw::ui
