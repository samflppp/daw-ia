#include "TestSupport.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/serialization/Json.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

// command -> envelope -> text -> envelope -> command, then compare payloads.
// This is the property the whole project rests on: a command that survives the
// trip can be replayed, versioned and driven from MCP with the same mechanism.
void checkRoundTrip(const CommandRegistry& registry, const Command& command)
{
    CommandEnvelope envelope{};
    envelope.id = CommandId::generate();
    envelope.type = std::string{command.type()};
    envelope.at = Timestamp::now();
    envelope.payload = command.payload();

    const auto text = json::write(envelope.toValue());
    const auto parsedValue = json::read(text);
    REQUIRE(parsedValue.ok());

    const auto parsed = CommandEnvelope::fromValue(parsedValue.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value().id == envelope.id);
    CHECK(parsed.value().type == envelope.type);
    CHECK(parsed.value().at == envelope.at);
    CHECK_FALSE(parsed.value().gesture.has_value());

    auto rebuilt = registry.create(parsed.value().type, parsed.value().payload);
    REQUIRE(rebuilt.ok());
    CHECK(rebuilt.value()->type() == command.type());
    CHECK(rebuilt.value()->payload() == command.payload());
}

} // namespace

TEST_CASE("Each command survives the round-trip through text")
{
    Harness harness;
    const auto clipId = ClipId::generate();

    checkRoundTrip(harness.registry, *harness.createClip(clipId, 4.0, 8.0));
    checkRoundTrip(harness.registry, *Harness::addNote(clipId, NoteId::generate(), 67));
    checkRoundTrip(harness.registry, *harness.setVolume(-7.5));
}

TEST_CASE("A gesture is carried by the envelope")
{
    const auto gesture = GestureId::generate();

    CommandEnvelope envelope{};
    envelope.id = CommandId::generate();
    envelope.type = "track.set_volume";
    envelope.at = Timestamp::now();
    envelope.gesture = gesture;
    envelope.payload =
        Value::object({{"trackId", Value{TrackId::generate().toString()}}, {"volumeDb", Value{-3.0}}});

    const auto parsed = CommandEnvelope::fromValue(envelope.toValue());
    REQUIRE(parsed.ok());
    REQUIRE(parsed.value().gesture.has_value());
    CHECK(*parsed.value().gesture == gesture);
}

TEST_CASE("A malformed envelope is refused")
{
    CommandEnvelope envelope{};
    envelope.id = CommandId::generate();
    envelope.type = "note.add";
    envelope.at = Timestamp::now();
    envelope.payload = Value::object({});

    SUBCASE("unsupported version")
    {
        auto value = envelope.toValue();
        REQUIRE(value.set("v", Value{std::int64_t{99}}).ok());
        CHECK(CommandEnvelope::fromValue(value).error().code == ErrorCode::invalidPayload);
    }

    SUBCASE("missing payload")
    {
        auto value = Value::object({{"v", Value{CommandEnvelope::currentVersion}},
                                    {"id", Value{envelope.id.toString()}},
                                    {"type", Value{envelope.type}},
                                    {"at", Value{envelope.at.microsSinceEpoch}}});
        CHECK(CommandEnvelope::fromValue(value).error().code == ErrorCode::invalidPayload);
    }

    SUBCASE("identifier that is not a ULID")
    {
        auto value = envelope.toValue();
        REQUIRE(value.set("id", Value{"pas-un-identifiant"}).ok());
        CHECK(CommandEnvelope::fromValue(value).error().code == ErrorCode::invalidPayload);
    }

    SUBCASE("empty type")
    {
        auto value = envelope.toValue();
        REQUIRE(value.set("type", Value{""}).ok());
        CHECK(CommandEnvelope::fromValue(value).error().code == ErrorCode::invalidPayload);
    }
}

TEST_CASE("A payload missing a field is refused by the factory, not by apply")
{
    Harness harness;

    const auto incomplete = harness.registry.create(
        "clip.create_midi", Value::object({{"trackId", Value{harness.trackId.toString()}}}));

    REQUIRE_FALSE(incomplete.ok());
    CHECK(incomplete.error().code == ErrorCode::invalidPayload);
}

TEST_CASE("An unknown type is an error with a name in it")
{
    Harness harness;
    const auto unknown = harness.registry.create("track.explode", Value::object({}));

    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.error().code == ErrorCode::unknownCommandType);
    CHECK(unknown.error().message.find("track.explode") != std::string::npos);
}

TEST_CASE("The registry holds the three project commands")
{
    const auto registry = CommandRegistry::withBuiltinCommands();

    CHECK(registry.contains("clip.create_midi"));
    CHECK(registry.contains("note.add"));
    CHECK(registry.contains("track.set_volume"));

    CommandRegistry other;
    REQUIRE(other.add<CreateMidiClip>().ok());
    CHECK(other.add<CreateMidiClip>().code() == ErrorCode::conflict);
}
