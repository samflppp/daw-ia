#pragma once

#include <cstdint>

namespace daw::domain
{

// Microseconds since the Unix epoch, UTC. Serialized as an integer: no locale,
// no text parsing, no ambiguity on the wire between core/ and the services.
struct Timestamp
{
    std::int64_t microsSinceEpoch{0};

    [[nodiscard]] static Timestamp now() noexcept;

    friend bool operator==(Timestamp lhs, Timestamp rhs) noexcept
    {
        return lhs.microsSinceEpoch == rhs.microsSinceEpoch;
    }

    friend bool operator<(Timestamp lhs, Timestamp rhs) noexcept
    {
        return lhs.microsSinceEpoch < rhs.microsSinceEpoch;
    }
};

} // namespace daw::domain
