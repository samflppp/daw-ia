#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace daw::domain
{

// Reference to a sequence of bytes held in the content-addressed store: the
// BLAKE3-256 digest of those bytes, and how many there are.
//
// The domain never holds heavy bytes by value. A plugin chunk reaches tens of
// megabytes, and the context an agent acted upon can be just as large; both
// leave the same eighty bytes in the project and in the journal.
//
// An empty digest means "nothing referenced" and is legal: a plugin with no
// captured state, a command with no agent context.
//
// The store that resolves a digest into bytes lives outside the domain, in
// core/engine: the domain names blobs, it does not read them.
struct BlobRef
{
    static constexpr std::size_t digestLength = 64; // BLAKE3-256, lowercase hex

    std::string digest;
    std::uint64_t byteCount{0};

    [[nodiscard]] bool isEmpty() const noexcept { return digest.empty(); }

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<BlobRef> fromValue(const Value& value);

    friend bool operator==(const BlobRef& lhs, const BlobRef& rhs);
    friend bool operator!=(const BlobRef& lhs, const BlobRef& rhs) { return !(lhs == rhs); }
};

} // namespace daw::domain
