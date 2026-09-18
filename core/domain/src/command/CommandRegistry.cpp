#include "daw/domain/command/CommandRegistry.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/CreateMidiClip.h"
#include "daw/domain/commands/SetTrackVolume.h"

#include <algorithm>

namespace daw::domain
{

CommandRegistry CommandRegistry::withBuiltinCommands()
{
    CommandRegistry registry;
    static_cast<void>(registry.add<CreateMidiClip>());
    static_cast<void>(registry.add<AddNote>());
    static_cast<void>(registry.add<SetTrackVolume>());
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
