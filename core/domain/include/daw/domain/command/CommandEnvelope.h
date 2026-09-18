#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Timestamp.h"
#include "daw/domain/Value.h"

#include <optional>
#include <string>

namespace daw::domain
{

// The serialized form of an executed command: identity, date, type, payload.
// This is the unit the journal stores, the unit JSON-RPC carries, and the unit
// a later version of the project replays.
//
//   {"v":1,"id":"01K...","type":"note.add","at":1758182400123456,
//    "gesture":null,"payload":{...}}
struct CommandEnvelope
{
    static constexpr std::int64_t currentVersion = 1;

    CommandId id{};
    std::string type;
    Timestamp at{};
    std::optional<GestureId> gesture;
    Value payload;

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<CommandEnvelope> fromValue(const Value& value);
};

} // namespace daw::domain
