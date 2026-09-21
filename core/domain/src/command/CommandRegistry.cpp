#include "daw/domain/command/CommandRegistry.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/commands/TransportCommands.h"

#include <algorithm>

namespace daw::domain
{

CommandRegistry CommandRegistry::withBuiltinCommands()
{
    CommandRegistry registry;
    static_cast<void>(registry.add<AddTrack>());
    static_cast<void>(registry.add<RemoveTrack>());
    static_cast<void>(registry.add<RenameTrack>());
    static_cast<void>(registry.add<ReorderTrack>());
    static_cast<void>(registry.add<CreateMidiClip>());
    static_cast<void>(registry.add<AddNote>());
    static_cast<void>(registry.add<RemoveNote>());
    static_cast<void>(registry.add<MoveNote>());
    static_cast<void>(registry.add<ResizeNote>());
    static_cast<void>(registry.add<SetNoteVelocity>());
    static_cast<void>(registry.add<QuantizeNotes>());
    static_cast<void>(registry.add<TransposeNotes>());
    static_cast<void>(registry.add<SetTrackVolume>());
    static_cast<void>(registry.add<SetTrackPan>());
    static_cast<void>(registry.add<SetTrackMuted>());
    static_cast<void>(registry.add<TransportPlay>());
    static_cast<void>(registry.add<TransportStop>());
    static_cast<void>(registry.add<TransportSetPosition>());
    static_cast<void>(registry.add<InsertPlugin>());
    static_cast<void>(registry.add<RemovePlugin>());
    static_cast<void>(registry.add<SetPluginBypassed>());
    static_cast<void>(registry.add<SetPluginParameter>());
    static_cast<void>(registry.add<InsertTempoPoint>());
    static_cast<void>(registry.add<RemoveTempoPoint>());
    static_cast<void>(registry.add<SetTempoPointBpm>());
    static_cast<void>(registry.add<MoveTempoPoint>());
    static_cast<void>(registry.add<CapturePluginState>());
    return registry;
}

Result<void> CommandRegistry::add(std::string type, Factory factory)
{
    if (type.empty())
        return fail(ErrorCode::invalidArgument, "empty command type");

    if (factory == nullptr)
        return fail(ErrorCode::invalidArgument, "no factory for " + type);

    if (contains(type))
        return fail(ErrorCode::conflict, "command type already registered: " + type);

    entries_.push_back(Entry{std::move(type), std::move(factory)});
    return {};
}

bool CommandRegistry::contains(std::string_view type) const noexcept
{
    return std::any_of(
        entries_.begin(), entries_.end(), [type](const Entry& entry) { return entry.type == type; });
}

std::vector<std::string> CommandRegistry::types() const
{
    std::vector<std::string> names;
    names.reserve(entries_.size());
    for (const auto& entry : entries_)
        names.push_back(entry.type);
    return names;
}

Result<std::unique_ptr<Command>> CommandRegistry::create(std::string_view type, const Value& payload) const
{
    for (const auto& entry : entries_)
    {
        if (entry.type == type)
            return entry.factory(payload);
    }

    return fail(ErrorCode::unknownCommandType, "no command registered for " + std::string{type});
}

} // namespace daw::domain
