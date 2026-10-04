#pragma once

#include "daw/domain/Value.h"

#include <cstdint>

namespace daw::domain::copilot
{

// What one request to the model cost, as the service reports it: the `usage`
// object of an answer, camelCase like the rest of the protocol —
// inputTokens, outputTokens, cacheReadTokens, cacheWriteTokens.
//
// Read here and nowhere else (S22). In S20 the mix read `input_tokens`, a key
// the service never sends, and counted zero tokens for every mix until the
// first real call to the model, in S21. A key that is missing reads zero.
struct Usage
{
    std::int64_t inputTokens{0};
    std::int64_t outputTokens{0};
    std::int64_t cacheReadTokens{0};
    std::int64_t cacheWriteTokens{0};

    [[nodiscard]] static Usage fromValue(const Value& value);
    [[nodiscard]] Value toValue() const;

    // Two rounds of a decision cost two requests: their tokens add up.
    [[nodiscard]] Usage plus(const Usage& other) const;

    friend bool operator==(const Usage& lhs, const Usage& rhs) = default;
};

} // namespace daw::domain::copilot
