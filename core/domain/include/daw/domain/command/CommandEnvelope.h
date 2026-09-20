#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Timestamp.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/Provenance.h"

#include <optional>
#include <string>

namespace daw::domain
{

// The serialized form of an executed command: identity, date, type, origin,
// payload. This is the unit the journal stores, the unit JSON-RPC carries, and
// the unit a later version of the project replays.
//
//   {"v":2,"id":"01K...","type":"note.add","at":1758182400123456,
//    "gesture":null,"origin":{"actor":"copilot","context":null},
//    "payload":{...}}
//
// Version 2 added "origin". It belongs here and not only in the SQLite journal
// for one reason: a replay reads envelopes, so a provenance living in a table
// column would be lost the moment a history travels as text — over JSON-RPC,
// in a bug report, between two versions of the project.
//
// Reading a v1 envelope is supported and always will be: it yields a "user"
// provenance, which is what those commands were. Writing is always v2.
struct CommandEnvelope
{
    static constexpr std::int64_t currentVersion = 2;
    static constexpr std::int64_t oldestReadableVersion = 1;

    CommandId id{};
    std::string type;
    Timestamp at{};
    std::optional<GestureId> gesture;
    Provenance origin{};
    Value payload;

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<CommandEnvelope> fromValue(const Value& value);
};

} // namespace daw::domain
