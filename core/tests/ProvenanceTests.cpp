#include "TestSupport.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/command/Provenance.h"
#include "daw/domain/serialization/Json.h"

#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
using daw::testing::RecordingObserver;

namespace
{

Provenance copilotWithContext()
{
    BlobRef context{};
    context.digest = std::string(BlobRef::digestLength, 'a');
    context.byteCount = 4096;

    Provenance origin{};
    origin.actor = Actor::copilot;
    origin.context = context;
    return origin;
}

Value withoutOrigin(const Value& envelope)
{
    Value::Object members;
    for (const auto& member : *envelope.asObject())
        if (member.first != "origin")
            members.emplace_back(member);
    return Value::object(std::move(members));
}

} // namespace

TEST_CASE("a command with no stated origin belongs to the user")
{
    Harness harness;
    RecordingObserver observer;
    const auto token = harness.bus.addObserver(observer);

    REQUIRE(harness.bus.execute(harness.setVolume(-3.0)).ok());
    harness.bus.removeObserver(token);

    REQUIRE(observer.executed.size() == 1);
    CHECK(observer.executed.front().origin.actor == Actor::user);
    CHECK(observer.executed.front().origin.isUser());
}

TEST_CASE("the origin travels in the envelope, not only in the receipt")
{
    Harness harness;
    REQUIRE(harness.bus.execute(harness.setVolume(-6.0), ExecuteOptions{{}, copilotWithContext()}).ok());

    const auto journal = harness.bus.journal();
    REQUIRE(journal.size() == 1);

    // Through text, the way a file or a socket carries it.
    const auto text = json::write(journal.front());
    auto parsed = json::read(text);
    REQUIRE(parsed.ok());

    auto envelope = CommandEnvelope::fromValue(parsed.value());
    REQUIRE(envelope.ok());
    CHECK(envelope.value().origin == copilotWithContext());

    auto version = parsed.value().intAt("v");
    REQUIRE(version.ok());
    CHECK(version.value() == CommandEnvelope::currentVersion);
}

TEST_CASE("a replayed journal keeps every provenance it mixed")
{
    Harness source;
    const auto clipId = ClipId::generate();
    REQUIRE(source.bus.execute(source.createClip(clipId)).ok());

    Provenance generator{};
    generator.actor = Actor::generator;
    REQUIRE(
        source.bus.execute(Harness::addNote(clipId, NoteId::generate(), 60), ExecuteOptions{{}, generator})
            .ok());
    REQUIRE(source.bus.execute(source.setVolume(-4.0), ExecuteOptions{{}, copilotWithContext()}).ok());

    // The replay runs on a project whose track carries the same identifier, so
    // the payloads of the source journal apply unchanged.
    Harness replayed;
    REQUIRE(replayed.state.removeTrack(replayed.trackId).ok());
    Track track{};
    track.id = source.trackId;
    track.name = "Piste 1";
    REQUIRE(replayed.state.addTrack(track).ok());

    for (const auto& envelope : source.bus.journal())
        REQUIRE(replayed.bus.executeSerialized(envelope).ok());

    const auto original = source.bus.journal();
    const auto again = replayed.bus.journal();
    REQUIRE(again.size() == original.size());
    for (std::size_t index = 0; index < again.size(); ++index)
        CHECK(json::write(again[index]) == json::write(original[index]));
}

TEST_CASE("a v1 envelope is read as a user command")
{
    Harness harness;
    REQUIRE(harness.bus.execute(harness.setVolume(-2.0)).ok());

    // Written before provenance existed: version 1, and no origin key at all.
    auto legacy = withoutOrigin(harness.bus.journal().front());
    REQUIRE(legacy.set("v", Value{std::int64_t{1}}).ok());

    auto parsed = CommandEnvelope::fromValue(legacy);
    REQUIRE(parsed.ok());
    CHECK(parsed.value().origin.actor == Actor::user);
    CHECK(!parsed.value().origin.context.has_value());

    Harness replayed;
    REQUIRE(replayed.state.removeTrack(replayed.trackId).ok());
    Track track{};
    track.id = harness.trackId;
    track.name = "Piste 1";
    REQUIRE(replayed.state.addTrack(track).ok());
    CHECK(replayed.bus.executeSerialized(legacy).ok());
}

TEST_CASE("a v2 envelope without an origin is malformed, not old")
{
    Harness harness;
    REQUIRE(harness.bus.execute(harness.setVolume(-2.0)).ok());

    auto parsed = CommandEnvelope::fromValue(withoutOrigin(harness.bus.journal().front()));
    REQUIRE(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::invalidPayload);
}

TEST_CASE("an unknown actor is refused instead of being rounded down to user")
{
    Harness harness;
    REQUIRE(harness.bus.execute(harness.setVolume(-2.0)).ok());

    auto envelope = harness.bus.journal().front();
    REQUIRE(envelope.set("origin", Value::object({{"actor", Value{"director"}}, {"context", Value{}}})).ok());

    auto parsed = CommandEnvelope::fromValue(envelope);
    REQUIRE(!parsed.ok());
    CHECK(parsed.error().code == ErrorCode::invalidPayload);
}

TEST_CASE("a context digest that is not a digest is refused")
{
    Provenance origin{};
    origin.actor = Actor::copilot;
    origin.context = BlobRef{"nope", 12};
    CHECK(!origin.validate().ok());

    Harness harness;
    auto executed = harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{{}, origin});
    REQUIRE(!executed.ok());
    CHECK(executed.error().code == ErrorCode::invalidArgument);
    CHECK(harness.bus.undoDepth() == 0);
}

TEST_CASE("a gesture does not merge two different origins")
{
    Harness harness;
    Provenance copilot{};
    copilot.actor = Actor::copilot;

    const auto gesture = harness.bus.beginGesture("fader volume");
    REQUIRE(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{gesture}).ok());

    auto merged = harness.bus.execute(harness.setVolume(-2.0), ExecuteOptions{gesture});
    REQUIRE(merged.ok());
    CHECK(merged.value().coalesced);
    CHECK(harness.bus.undoDepth() == 1);

    auto foreign = harness.bus.execute(harness.setVolume(-3.0), ExecuteOptions{gesture, copilot});
    REQUIRE(foreign.ok());
    CHECK(!foreign.value().coalesced);
    CHECK(foreign.value().origin.actor == Actor::copilot);
    CHECK(harness.bus.undoDepth() == 2);
    REQUIRE(harness.bus.endGesture(gesture).ok());
}

TEST_CASE("an undo reports who asked for it, and never rewrites the author")
{
    Harness harness;
    RecordingObserver observer;
    const auto token = harness.bus.addObserver(observer);

    Provenance generator{};
    generator.actor = Actor::generator;
    REQUIRE(harness.bus.execute(harness.setVolume(-5.0), ExecuteOptions{{}, generator}).ok());

    Provenance copilot{};
    copilot.actor = Actor::copilot;
    REQUIRE(harness.bus.undo(copilot).ok());
    REQUIRE(harness.bus.redo().ok());
    harness.bus.removeObserver(token);

    REQUIRE(observer.undone.size() == 1);
    CHECK(observer.undone.front().origin.actor == Actor::copilot);
    CHECK(observer.undone.front().payload.isNull());

    REQUIRE(observer.redone.size() == 1);
    CHECK(observer.redone.front().origin.actor == Actor::user);

    // The entry itself still belongs to the generator that wrote it.
    const auto journal = harness.bus.journal();
    REQUIRE(journal.size() == 1);
    auto envelope = CommandEnvelope::fromValue(journal.front());
    REQUIRE(envelope.ok());
    CHECK(envelope.value().origin.actor == Actor::generator);
}

TEST_CASE("a receipt carries the payload the journal will store")
{
    Harness harness;
    RecordingObserver observer;
    const auto token = harness.bus.addObserver(observer);

    const auto gesture = harness.bus.beginGesture("fader volume");
    REQUIRE(harness.bus.execute(harness.setVolume(-1.0), ExecuteOptions{gesture}).ok());
    REQUIRE(harness.bus.execute(harness.setVolume(-7.5), ExecuteOptions{gesture}).ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());
    harness.bus.removeObserver(token);

    REQUIRE(observer.executed.size() == 1);
    REQUIRE(observer.coalesced.size() == 1);

    // The merged payload, the one a replay must use.
    auto volume = observer.coalesced.front().payload.doubleAt("volumeDb");
    REQUIRE(volume.ok());
    CHECK(volume.value() == doctest::Approx(-7.5));
}
