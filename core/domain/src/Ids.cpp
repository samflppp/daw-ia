#include "daw/domain/Ids.h"

#include "daw/domain/Timestamp.h"

#include <random>

namespace daw::domain
{
namespace
{

// Crockford base32: no I, L, O or U, so a written identifier cannot be misread.
constexpr std::string_view alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// 26 characters carry 130 bits; the identifier is 128. The two extra bits are
// the high bits of the first character, and they must be zero.
constexpr std::size_t paddingBits = 2;

std::uint64_t randomBits()
{
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine();
}

int decodeCharacter(char character) noexcept
{
    const auto upper =
        (character >= 'a' && character <= 'z') ? static_cast<char>(character - ('a' - 'A')) : character;

    // Crockford decoding also accepts the letters excluded from the alphabet.
    switch (upper)
    {
    case 'I':
    case 'L':
        return 1;
    case 'O':
        return 0;
    default:
        break;
    }

    const auto position = alphabet.find(upper);
    if (position == std::string_view::npos)
        return -1;
    return static_cast<int>(position);
}

bool bitAt(const std::array<std::uint8_t, 16>& bytes, std::size_t streamIndex) noexcept
{
    if (streamIndex < paddingBits)
        return false;

    const auto bitIndex = streamIndex - paddingBits;
    const auto byte = bytes[bitIndex / 8];
    const auto shift = 7U - static_cast<unsigned>(bitIndex % 8);
    return ((byte >> shift) & 1U) != 0U;
}

void setBit(std::array<std::uint8_t, 16>& bytes, std::size_t streamIndex) noexcept
{
    if (streamIndex < paddingBits)
        return;

    const auto bitIndex = streamIndex - paddingBits;
    const auto shift = 7U - static_cast<unsigned>(bitIndex % 8);
    bytes[bitIndex / 8] = static_cast<std::uint8_t>(bytes[bitIndex / 8] | (1U << shift));
}

} // namespace

Ulid Ulid::generate()
{
    const auto micros = Timestamp::now().microsSinceEpoch;
    const auto millis = static_cast<std::uint64_t>(micros >= 0 ? micros / 1000 : 0);

    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t index = 0; index < 6; ++index)
    {
        const auto shift = static_cast<unsigned>(8 * (5 - index));
        bytes[index] = static_cast<std::uint8_t>((millis >> shift) & 0xFFU);
    }

    const auto high = randomBits();
    const auto low = randomBits();
    for (std::size_t index = 0; index < 8; ++index)
    {
        const auto shift = static_cast<unsigned>(8 * (7 - index));
        bytes[6 + index] = static_cast<std::uint8_t>((high >> shift) & 0xFFU);
    }
    bytes[14] = static_cast<std::uint8_t>((low >> 8) & 0xFFU);
    bytes[15] = static_cast<std::uint8_t>(low & 0xFFU);

    return Ulid{bytes};
}

Result<Ulid> Ulid::parse(std::string_view text)
{
    if (text.size() != textLength)
        return fail(ErrorCode::invalidPayload,
                    "identifier must be 26 characters, got " + std::to_string(text.size()));

    std::array<std::uint8_t, 16> bytes{};
    for (std::size_t characterIndex = 0; characterIndex < textLength; ++characterIndex)
    {
        const auto digit = decodeCharacter(text[characterIndex]);
        if (digit < 0)
            return fail(ErrorCode::invalidPayload,
                        std::string{"invalid character in identifier: "} + text[characterIndex]);

        if (characterIndex == 0 && digit > 7)
            return fail(ErrorCode::invalidPayload, "identifier overflows 128 bits");

        for (std::size_t bit = 0; bit < 5; ++bit)
        {
            const auto shift = static_cast<unsigned>(4 - bit);
            if (((static_cast<unsigned>(digit) >> shift) & 1U) != 0U)
                setBit(bytes, characterIndex * 5 + bit);
        }
    }

    return Ulid{bytes};
}

std::string Ulid::toString() const
{
    std::string text(textLength, '0');
    for (std::size_t characterIndex = 0; characterIndex < textLength; ++characterIndex)
    {
        unsigned digit = 0;
        for (std::size_t bit = 0; bit < 5; ++bit)
        {
            digit = static_cast<unsigned>(digit << 1U);
            if (bitAt(bytes_, characterIndex * 5 + bit))
                digit |= 1U;
        }
        text[characterIndex] = alphabet[digit];
    }
    return text;
}

bool Ulid::isNil() const noexcept
{
    for (const auto byte : bytes_)
    {
        if (byte != 0)
            return false;
    }
    return true;
}

} // namespace daw::domain
