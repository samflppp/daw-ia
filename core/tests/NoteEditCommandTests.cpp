#include "TestSupport.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/domain/serialization/Json.h"

#include <memory>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

// A clip with three notes, deliberately off the grid and in a chord shape, so
// quantizing and transposing both have something to change.
struct Phrase
{
    ClipId clipId{ClipId::generate()};
    std::vector<NoteId> noteIds;

    void record(Harness& harness, std::vector<double> starts, std::vector<int> pitches)
    {
        REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 8.0)).ok());

        for (std::size_t index = 0; index < starts.size(); ++index)
        {
            Note note{};
            note.id = NoteId::generate();
            note.pitch = pitches[index];
            note.velocity = 100;
            note.startBeats = starts[index];
            note.lengthBeats = 0.5;
            REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clipId, note)).ok());
            noteIds.push_back(note.id);
        }
    }

    [[nodiscard]] const Note& note(const Harness& harness, std::size_t index) const
    {
        const auto* clip = harness.state.findClip(clipId);
        REQUIRE(clip != nullptr);
        REQUIRE(clip->notes.size() > index);
        return clip->notes[index];
    }
};

} // namespace

TEST_CASE("a velocity is edited, not only read")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.0}, {60});

    REQUIRE(
        harness.bus.execute(std::make_unique<SetNoteVelocity>(phrase.clipId, phrase.noteIds[0], 40)).ok());
    CHECK(phrase.note(harness, 0).velocity == 40);

    REQUIRE(harness.bus.undo().ok());
    CHECK(phrase.note(harness, 0).velocity == 100);

    CHECK(harness.bus.execute(std::make_unique<SetNoteVelocity>(phrase.clipId, phrase.noteIds[0], 0))
              .error()
              .code == ErrorCode::invalidArgument);
    CHECK(harness.bus.execute(std::make_unique<SetNoteVelocity>(phrase.clipId, phrase.noteIds[0], 128))
              .error()
              .code == ErrorCode::invalidArgument);
}

TEST_CASE("a velocity drag is one entry, and two notes stay two")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.0, 1.0}, {60, 64});

    const auto before = harness.bus.undoDepth();

    const auto gesture = harness.bus.beginGesture("velocite");
    for (int step = 100; step > 60; --step)
        REQUIRE(harness.bus
                    .execute(std::make_unique<SetNoteVelocity>(phrase.clipId, phrase.noteIds[0], step),
                             ExecuteOptions{gesture})
                    .ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<SetNoteVelocity>(phrase.clipId, phrase.noteIds[1], 20),
                         ExecuteOptions{gesture})
                .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == before + 2);

    REQUIRE(harness.bus.undo().ok());
    CHECK(phrase.note(harness, 1).velocity == 100);
    CHECK(phrase.note(harness, 0).velocity == 61);
}

TEST_CASE("quantizing snaps starts onto the grid and leaves lengths alone")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.07, 0.98, 2.13}, {60, 62, 64});

    REQUIRE(harness.bus.execute(std::make_unique<QuantizeNotes>(phrase.clipId, phrase.noteIds, 0.5)).ok());

    CHECK(phrase.note(harness, 0).startBeats == doctest::Approx(0.0));
    CHECK(phrase.note(harness, 1).startBeats == doctest::Approx(1.0));
    CHECK(phrase.note(harness, 2).startBeats == doctest::Approx(2.0));

    // Lengths untouched: quantizing starts and quantizing durations are two
    // different musical decisions.
    CHECK(phrase.note(harness, 0).lengthBeats == doctest::Approx(0.5));

    REQUIRE(harness.bus.undo().ok());
    CHECK(phrase.note(harness, 0).startBeats == doctest::Approx(0.07));
    CHECK(phrase.note(harness, 2).startBeats == doctest::Approx(2.13));
}

TEST_CASE("quantizing names its notes, and touches no other")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.07, 0.98}, {60, 62});

    REQUIRE(harness.bus
                .execute(std::make_unique<QuantizeNotes>(
                    phrase.clipId, std::vector<NoteId>{phrase.noteIds[0]}, 1.0))
                .ok());

    CHECK(phrase.note(harness, 0).startBeats == doctest::Approx(0.0));
    CHECK(phrase.note(harness, 1).startBeats == doctest::Approx(0.98));
}

TEST_CASE("a quantize that names an absent note moves nothing at all")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.07, 0.98}, {60, 62});

    const auto before = harness.state.toValue();

    auto refused = harness.bus.execute(std::make_unique<QuantizeNotes>(
        phrase.clipId, std::vector<NoteId>{phrase.noteIds[0], NoteId::generate()}, 1.0));
    CHECK(refused.error().code == ErrorCode::notFound);

    // All-or-nothing: the first note did not move either.
    CHECK(harness.state.toValue() == before);
    CHECK(harness.bus.undoDepth() == 3);

    CHECK(harness.bus.execute(std::make_unique<QuantizeNotes>(phrase.clipId, phrase.noteIds, 0.0))
              .error()
              .code == ErrorCode::invalidArgument);
}

TEST_CASE("transposing moves every named note by the same interval")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.0, 0.0, 0.0}, {60, 64, 67});

    REQUIRE(harness.bus.execute(std::make_unique<TransposeNotes>(phrase.clipId, phrase.noteIds, 5)).ok());

    CHECK(phrase.note(harness, 0).pitch == 65);
    CHECK(phrase.note(harness, 1).pitch == 69);
    CHECK(phrase.note(harness, 2).pitch == 72);
    CHECK(phrase.note(harness, 0).startBeats == doctest::Approx(0.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(phrase.note(harness, 0).pitch == 60);
    CHECK(phrase.note(harness, 2).pitch == 67);
}

TEST_CASE("a transposition that would leave the keyboard is refused whole")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.0, 0.0}, {60, 125});

    const auto before = harness.state.toValue();

    auto refused = harness.bus.execute(std::make_unique<TransposeNotes>(phrase.clipId, phrase.noteIds, 6));
    CHECK(refused.error().code == ErrorCode::invalidArgument);

    // Not clamped: a transposition down and back up would otherwise give back
    // a chord the user never played, and undo would stop being the inverse.
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("the editing verbs are rebuilt from their payloads alone")
{
    Harness harness;
    Phrase phrase;
    phrase.record(harness, {0.07, 0.98, 2.13}, {60, 64, 67});

    REQUIRE(harness.bus.execute(std::make_unique<QuantizeNotes>(phrase.clipId, phrase.noteIds, 0.25)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<TransposeNotes>(phrase.clipId, phrase.noteIds, -12)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<SetNoteVelocity>(phrase.clipId, phrase.noteIds[1], 30)).ok());

    std::vector<std::string> lines;
    for (const auto& envelope : harness.bus.journal())
        lines.push_back(json::write(envelope));

    ProjectState replayed;
    const auto registry = CommandRegistry::withBuiltinCommands();
    CommandBus bus{replayed, registry};
    REQUIRE(bus.execute(std::make_unique<AddTrack>(harness.trackId, "Piste 1", 0.0)).ok());

    for (const auto& line : lines)
    {
        const auto envelope = json::read(line);
        REQUIRE(envelope.ok());
        REQUIRE(bus.executeSerialized(envelope.value()).ok());
    }

    CHECK(replayed == harness.state);
}

TEST_CASE("a payload naming no note is refused rather than treated as all of them")
{
    const auto registry = CommandRegistry::withBuiltinCommands();
    const auto clipId = ClipId::generate();

    CHECK(registry
              .create(QuantizeNotes::commandType,
                      Value::object({{"clipId", Value{clipId.toString()}},
                                     {"noteIds", Value::array({})},
                                     {"gridBeats", Value{0.25}}}))
              .error()
              .code == ErrorCode::invalidPayload);

    CHECK(registry
              .create(TransposeNotes::commandType,
                      Value::object({{"clipId", Value{clipId.toString()}}, {"semitones", Value{2}}}))
              .error()
              .code == ErrorCode::invalidPayload);
}
