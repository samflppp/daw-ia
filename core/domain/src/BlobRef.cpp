#include "daw/domain/BlobRef.h"

#include <algorithm>

namespace daw::domain
{

Result<void> BlobRef::validate() const
{
    if (digest.empty())
    {
        if (byteCount != 0)
            return fail(ErrorCode::invalidArgument, "blob has a size but no digest");
        return {};
    }

    if (digest.size() != digestLength)
        return fail(ErrorCode::invalidArgument, "blob digest is not a BLAKE3-256 hex digest");

    const bool hex =
        std::all_of(digest.begin(),
                    digest.end(),
                    [](unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
    if (!hex)
        return fail(ErrorCode::invalidArgument, "blob digest is not lowercase hexadecimal");

    if (byteCount == 0)
        return fail(ErrorCode::invalidArgument, "blob digest refers to zero bytes");

    return {};
}

Value BlobRef::toValue() const
{
    return Value::object(
        {{"digest", Value{digest}}, {"byteCount", Value{static_cast<std::int64_t>(byteCount)}}});
}

Result<BlobRef> BlobRef::fromValue(const Value& value)
{
    auto digest = value.stringAt("digest");
    if (!digest)
        return digest.error();

    auto byteCount = value.intAt("byteCount");
    if (!byteCount)
        return byteCount.error();

    if (byteCount.value() < 0)
        return fail(ErrorCode::invalidPayload, "byteCount is negative");

    BlobRef reference{};
    reference.digest = digest.value();
    reference.byteCount = static_cast<std::uint64_t>(byteCount.value());

    auto valid = reference.validate();
    if (!valid)
        return valid.error();

    return reference;
}

bool operator==(const BlobRef& lhs, const BlobRef& rhs)
{
    return lhs.digest == rhs.digest && lhs.byteCount == rhs.byteCount;
}

} // namespace daw::domain
