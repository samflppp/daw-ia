#include "daw/domain/Ids.h"

#include <set>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;

TEST_CASE("A generated identifier is 26 characters and survives a round-trip")
{
    const auto id = Ulid::generate();
    const auto text = id.toString();

    CHECK(text.size() == Ulid::textLength);
    CHECK_FALSE(id.isNil());

    const auto parsed = Ulid::parse(text);
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == id);
    CHECK(parsed.value().toString() == text);
}

TEST_CASE("Identifiers are unique")
{
    std::set<std::string> seen;
    for (int index = 0; index < 1000; ++index)
        seen.insert(Ulid::generate().toString());

    CHECK(seen.size() == 1000);
}

TEST_CASE("Malformed identifiers are refused")
{
    CHECK_FALSE(Ulid::parse("").ok());
    CHECK_FALSE(Ulid::parse("TROPCOURT").ok());
    CHECK_FALSE(Ulid::parse("01ARZ3NDEKTSV4RRFFQ69G5FA").ok());   // 25 characters
    CHECK_FALSE(Ulid::parse("01ARZ3NDEKTSV4RRFFQ69G5FAVX").ok()); // 27 characters
    CHECK_FALSE(Ulid::parse("01ARZ3NDEKTSV4RRFFQ69G5FA!").ok());  // invalid character
    CHECK_FALSE(Ulid::parse("81ARZ3NDEKTSV4RRFFQ69G5FAV").ok());  // overflows 128 bits
}

TEST_CASE("A known identifier decodes and re-encodes identically")
{
    const auto parsed = Ulid::parse("01ARZ3NDEKTSV4RRFFQ69G5FAV");
    REQUIRE(parsed.ok());
    CHECK(parsed.value().toString() == "01ARZ3NDEKTSV4RRFFQ69G5FAV");
}

TEST_CASE("Entity identifiers of different kinds do not mix")
{
    const auto text = Ulid::generate().toString();

    const auto trackId = TrackId::parse(text);
    const auto clipId = ClipId::parse(text);

    REQUIRE(trackId.ok());
    REQUIRE(clipId.ok());

    // Same bits, different types: the compiler refuses to compare them, which
    // is exactly the point. Only the textual form is shared.
    CHECK(trackId.value().toString() == clipId.value().toString());

    const TrackId nil{};
    CHECK(nil.isNil());
    CHECK_FALSE(trackId.value().isNil());
}
