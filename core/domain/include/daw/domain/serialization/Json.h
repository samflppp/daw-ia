#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <string>
#include <string_view>

namespace daw::domain::json
{

// Text form of a Value. The JSON library used underneath is an implementation
// detail of Json.cpp: it appears in no header, so no other module inherits it
// and it can be swapped without touching a single command.
//
// write() is deterministic: object members are already sorted inside Value, and
// nothing here reorders them.
[[nodiscard]] std::string write(const Value& value);
[[nodiscard]] std::string writePretty(const Value& value);

[[nodiscard]] Result<Value> read(std::string_view text);

} // namespace daw::domain::json
