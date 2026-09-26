#include "daw/ui/history/HistoryLog.h"

#include <algorithm>
#include <iterator>
#include <utility>

namespace daw::ui
{
namespace
{

// One label per command the registry knows. Kept next to nothing else, so that
// adding a command and forgetting its label shows the command's own name in
// the panel rather than a wrong sentence.
constexpr std::pair<std::string_view, std::string_view> labels[] = {
    {"track.add", "Nouvelle piste"},
    {"track.remove", "Piste supprimée"},
    {"track.rename", "Piste renommée"},
    {"track.reorder", "Piste déplacée"},
    {"track.set_volume", "Volume de piste"},
    {"track.set_pan", "Panoramique"},
    {"track.set_muted", "Piste coupée"},
    {"track.set_channel_pitch", "Hauteur du canal"},
    {"track.set_sample", "Sample du canal"},
    {"audio.place", "Clip audio posé"},
    {"audio.move", "Clip audio déplacé"},
    {"audio.remove", "Clip audio retiré"},
    {"clip.create_midi", "Nouveau clip"},
    {"note.add", "Note ajoutée"},
    {"note.remove", "Note effacée"},
    {"note.move", "Note déplacée"},
    {"note.resize", "Note allongée"},
    {"note.set_velocity", "Vélocité"},
    {"note.quantize", "Quantification"},
    {"note.transpose", "Transposition"},
    {"pattern.create", "Nouveau pattern"},
    {"pattern.place", "Pattern posé"},
    {"pattern.add_track", "Ligne ouverte"},
    {"pattern.set_length", "Longueur du pattern"},
    {"pattern.rename", "Pattern renommé"},
    {"pattern.remove", "Pattern supprimé"},
    {"placement.move", "Pose déplacée"},
    {"placement.remove", "Pose retirée"},
    {"tempo.insert", "Changement de tempo"},
    {"tempo.remove", "Tempo retiré"},
    {"tempo.set_bpm", "Tempo"},
    {"tempo.move", "Tempo déplacé"},
    {"project.set_time_signature", "Signature rythmique"},
    {"automation.create_line", "Ligne d'automation"},
    {"automation.remove_line", "Automation supprimée"},
    {"automation.add_point", "Point d'automation"},
    {"automation.move_point", "Point d'automation déplacé"},
    {"automation.remove_point", "Point d'automation retiré"},
    {"automation.set_curve", "Courbe d'automation"},
    {"automation.write", "Automation écrite"},
    {"plugin.insert", "Plugin inséré"},
    {"plugin.remove", "Plugin retiré"},
    {"plugin.set_bypassed", "Plugin contourné"},
    {"plugin.set_parameter", "Paramètre de plugin"},
    {"plugin.capture_state", "État de plugin"},
    {"transport.play", "Lecture"},
    {"transport.stop", "Arrêt"},
    {"transport.set_position", "Position"},
    {"transport.set_loop", "Boucle"},
    {"transport.set_mode", "Mode de lecture"},
};

} // namespace

std::string_view HistoryLog::Entry::label() const noexcept
{
    // The group label is the sentence that was asked; the type is what the
    // command was called. When both exist the first one is the one that tells
    // the user what they can undo.
    if (group.has_value())
        return group->label;

    return HistoryLog::describe(type);
}

std::string_view HistoryLog::describe(std::string_view type) noexcept
{
    const auto found = std::find_if(
        std::begin(labels), std::end(labels), [type](const auto& pair) { return pair.first == type; });

    return found != std::end(labels) ? found->second : type;
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

    // The commands of a group are notified one by one, because the journal
    // needs every payload. Here they are one line: the second command of a
    // group joins the entry the first one opened.
    if (receipt.group.has_value() && cursor_ > 0)
    {
        auto& top = entries_[cursor_ - 1];
        if (top.group.has_value() && top.group->id == receipt.group->id)
        {
            ++top.merged;
            return;
        }
    }

    // Executing after an undo kills the redo branch, in the bus and here.
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());

    Entry entry{};
    entry.id = receipt.id;
    entry.type = receipt.type;
    entry.at = receipt.at;
    entry.actor = receipt.origin.actor;
    entry.group = receipt.group;
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
