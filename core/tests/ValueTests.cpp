#include "daw/domain/Value.h"
#include "daw/domain/serialization/Json.h"

#include <doctest/doctest.h>

using namespace daw::domain;

TEST_CASE("Value keeps integers and reals apart")
{
    const Value integer{std::int64_t{7}};
    const Value real{7.0};

    CHECK(integer.kind() == Value::Kind::integer);
    CHECK(real.kind() == Value::Kind::number);
    CHECK(integer != real);

    // A reader asking for a number accepts an integer: a JSON writer is free to
    // drop a trailing ".0", and a round-trip must not change the meaning.
    REQUIRE(integer.asDouble().ok());
    CHECK(integer.asDouble().value() == doctest::Approx(7.0));
    CHECK_FALSE(real.asInt().ok());
}

TEST_CASE("Object members are sorted, so equal values serialize the same")
{
    const auto first = Value::object({{"b", Value{1}}, {"a", Value{2}}});
    const auto second = Value::object({{"a", Value{2}}, {"b", Value{1}}});

    CHECK(first == second);
    CHECK(json::write(first) == json::write(second));

    const auto* members = first.asObject();
    REQUIRE(members != nullptr);
    REQUIRE(members->size() == 2);
    CHECK((*members)[0].first == "a");
    CHECK((*members)[1].first == "b");
}

TEST_CASE("Typed readers name the key and the reason")
{
    const auto value = Value::object({{"pitch", Value{"soixante"}}});

    const auto missing = value.intAt("velocity");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::invalidPayload);

    const auto wrongKind = value.intAt("pitch");
    REQUIRE_FALSE(wrongKind.ok());
    CHECK(wrongKind.error().code == ErrorCode::typeMismatch);

    const Value notAnObject{42};
    CHECK(notAnObject.intAt("pitch").error().code == ErrorCode::typeMismatch);
}

TEST_CASE("set and push grow a null value, and refuse the wrong kind")
{
    Value object{};
    REQUIRE(object.set("a", Value{1}).ok());
    REQUIRE(object.set("a", Value{2}).ok()); // replaces, does not duplicate
    CHECK(object.size() == 1);
    CHECK(object.intAt("a").value() == 2);

    Value array{};
    REQUIRE(array.push(Value{1}).ok());
    REQUIRE(array.push(Value{2}).ok());
    CHECK(array.size() == 2);

    CHECK_FALSE(object.push(Value{3}).ok());
    CHECK_FALSE(array.set("a", Value{3}).ok());
}

TEST_CASE("Copying a Value copies its children")
{
    auto original = Value::object({{"notes", Value::array({Value{1}, Value{2}})}});
    auto copy = original;

    REQUIRE(original.set("notes", Value::array({Value{9}})).ok());

    CHECK(copy.find("notes")->size() == 2);
    CHECK(original.find("notes")->size() == 1);
}

TEST_CASE("JSON round-trip preserves the value")
{
    const auto value = Value::object({{"tempo", Value{93.5}},
                                      {"bars", Value{std::int64_t{8}}},
                                      {"swing", Value{true}},
                                      {"name", Value{"boucle"}},
                                      {"empty", Value{}},
                                      {"notes", Value::array({Value{60}, Value{64}, Value{67}})}});

    const auto text = json::write(value);
    const auto parsed = json::read(text);

    REQUIRE(parsed.ok());
    CHECK(parsed.value() == value);
}

TEST_CASE("Malformed JSON is an error, never an exception")
{
    const auto parsed = json::read("{\"tempo\":");
    REQUIRE_FALSE(parsed.ok());
    CHECK(parsed.error().code == ErrorCode::serialisationError);
}
