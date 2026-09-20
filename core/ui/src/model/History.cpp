#include "daw/ui/model/History.h"

namespace daw::ui
{

void History::onExecuted(const domain::Receipt& receipt)
{
    HistoryLog::onExecuted(receipt);
    sendChangeMessage();
}

void History::onCoalesced(const domain::Receipt& receipt)
{
    HistoryLog::onCoalesced(receipt);
    sendChangeMessage();
}

void History::onUndone(const domain::Receipt& receipt)
{
    HistoryLog::onUndone(receipt);
    sendChangeMessage();
}

void History::onRedone(const domain::Receipt& receipt)
{
    HistoryLog::onRedone(receipt);
    sendChangeMessage();
}

void History::onHistoryTruncated(std::size_t droppedEntries)
{
    HistoryLog::onHistoryTruncated(droppedEntries);
    sendChangeMessage();
}

} // namespace daw::ui
