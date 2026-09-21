#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Timestamp.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/CommandGroup.h"
#include "daw/domain/command/Provenance.h"

#include <optional>
#include <string>

namespace daw::domain
{

// The serialized form of an executed command: identity, date, type, origin,
// payload. This is the unit the journal stores, the unit JSON-RPC carries, and
// the unit a later version of the project replays.
//
//   {"v":3,"id":"01K...","type":"note.add","at":1758182400123456,
//    "gesture":null,"group":null,"origin":{"actor":"copilot","context":null},
//    "payload":{...}}
//
// Version 2 added "origin". It belongs here and not only in the SQLite journal
// for one reason: a replay reads envelopes, so a provenance living in a table
// column would be lost the moment a history travels as text — over JSON-RPC,
// in a bug report, between two versions of the project.
//
// Version 3 added "group", for the same reason and with the same consequence:
// a group is what makes several commands one history entry, so a replay that
// could not read it would rebuild the same project with a different history —
// one Ctrl+Z where there used to be one becomes three.
//
// It sits next to "gesture" and not in its place. A gesture merges commands
// that are the same edit repeated and keeps one of them; a group keeps every
// command it holds and merely gives them one entry. Storing both in one field
// would make the journal unable to say which of the two happened.
//
// Reading a v1 envelope is supported and always will be: it yields a "user"
// provenance, which is what those commands were. A v2 envelope has no group,
// which is what those commands had. Writing is always v3.
struct CommandEnvelope
{
    static constexpr std::int64_t currentVersion = 3;
    static constexpr std::int64_t oldestReadableVersion = 1;

    CommandId id{};
    std::string type;
    Timestamp at{};
    std::optional<GestureId> gesture;
    std::optional<GroupRef> group;
    Provenance origin{};
    Value payload;

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<CommandEnvelope> fromValue(const Value& value);
};

} // namespace daw::domain
