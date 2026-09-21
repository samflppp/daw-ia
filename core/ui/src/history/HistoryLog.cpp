#include "daw/ui/history/HistoryLog.h"

#include <algorithm>
#include <array>
#include <utility>

namespace daw::ui
{
namespace
{

// One label per command the registry knows. Kept next to nothing else, so that
// adding a command and forgetting its label shows the command's own name in
// the panel rather than a wrong sentence.
constexpr std::array<std::pair<std::string_view, std::string_view>, 17> labels{{
    {"track.add", "Nouvelle piste"},
    {"track.remove", "Piste supprimée"},
    {"track.set_volume", "Volume de piste"},
    {"track.set_muted", "Piste coupée"},
    {"clip.create_midi", "Nouveau clip"},
    {"note.add", "Note ajoutée"},
    {"note.remove", "Note effacée"},
    {"note.move", "Note déplacée"},
    {"note.resize", "Note allongée"},
    {"plugin.insert", "Plugin inséré"},
    {"plugin.remove", "Plugin retiré"},
    {"plugin.set_bypassed", "Plugin contourné"},
    {"plugin.set_parameter", "Paramètre de plugin"},
    {"plugin.capture_state", "État de plugin"},
    {"transport.play", "Lecture"},
    {"transport.stop", "Arrêt"},
    {"transport.set_position", "Position"},
}};

} // namespace

std::string_view HistoryLog::describe(std::string_view type) noexcept
{
    const auto found =
        std::find_if(labels.begin(), labels.end(), [type](const auto& pair) { return pair.first == type; });

    return found != labels.end() ? found->second : type;
}

void HistoryLog::clear() noexcept
{
    entries_.clear();
    cursor_ = 0;
}

void HistoryLog::onExecuted(const domain::Receipt& receipt)
{
    // A transport command is executed and notified like the others and leaves
    // no history entry. Showing it here would put a line in the panel that no
    // undo can ever reach.
    if (receipt.policy == domain::HistoryPolicy::transient)
        return;

    // Executing after an undo kills the redo branch, in the bus and here.
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());

    Entry entry{};
    entry.id = receipt.id;
    entry.type = receipt.type;
    entry.at = receipt.at;
    entry.actor = receipt.origin.actor;
    entries_.push_back(std::move(entry));
    cursor_ = entries_.size();
}

void HistoryLog::onCoalesced(const domain::Receipt& receipt)
{
    // A coalesced command joined the entry on top instead of creating one, so
    // the list does not grow: the sixty steps of a fader sweep are one line
    // that says it holds sixty.
    if (cursor_ == 0)
        return;

    auto& top = entries_[cursor_ - 1];
    if (top.id != receipt.id)
        return;

    ++top.merged;
}

void HistoryLog::onUndone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);

    if (cursor_ > 0)
        --cursor_;
}

void HistoryLog::onRedone(const domain::Receipt& receipt)
{
    static_cast<void>(receipt);

    if (cursor_ < entries_.size())
        ++cursor_;
}

void HistoryLog::onHistoryTruncated(std::size_t droppedEntries)
{
    // The bus drops the oldest entries when the stack is too deep. They go
    // from here too, or the panel would offer to come back to a state the bus
    // can no longer reach.
    const auto dropped = std::min(droppedEntries, entries_.size());
    entries_.erase(entries_.begin(), entries_.begin() + static_cast<std::ptrdiff_t>(dropped));
    cursor_ -= std::min(cursor_, dropped);
}

} // namespace daw::ui
