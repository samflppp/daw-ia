#include "TestSupport.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/serialization/Json.h"

#include <initializer_list>
#include <memory>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

Note makeNote(NoteId id, int pitch, double start)
{
    Note note{};
    note.id = id;
    note.pitch = pitch;
    note.velocity = 100;
    note.startBeats = start;
    note.lengthBeats = 0.25;
    return note;
}

} // namespace

TEST_CASE("clip.create_midi builds a pattern of one row and lays it down")
{
    Harness harness;
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 8.0, 4.0)).ok());

    // The two identifiers it did not receive are derived from the one it did,
    // never drawn: eight weeks of journals name only the clip, and a replay
    // that invented them would build a different project every time it ran.
    const auto patternId = ProjectState::patternIdForClip(clipId);
    const auto placementId = ProjectState::placementIdForClip(clipId);

    const auto* pattern = harness.state.findPattern(patternId);
    REQUIRE(pattern != nullptr);
    CHECK(pattern->lengthBeats == doctest::Approx(4.0));
    REQUIRE(pattern->clips.size() == 1);
    CHECK(pattern->clips.front().id == clipId);
    CHECK(pattern->clips.front().trackId == harness.trackId);

    const auto* placement = harness.state.findPlacement(placementId);
    REQUIRE(placement != nullptr);
    CHECK(placement->patternId == patternId);
    CHECK(placement->startBeats == doctest::Approx(8.0));

    // The row is reachable by the identifier every note command already uses.
    REQUIRE(harness.state.findClip(clipId) != nullptr);
    CHECK(harness.state.patternOfClip(clipId).value() == patternId);

    // Undoing takes all three away, and leaves nothing behind.
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.patterns().empty());
    CHECK(harness.state.arrangement().empty());
    CHECK(harness.state.findClip(clipId) == nullptr);
}

TEST_CASE("a pattern laid eight times is modified by one command")
{
    Harness harness;
    const auto patternId = PatternId::generate();
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddPatternTrack>(patternId, clipId, harness.trackId)).ok());

    for (int index = 0; index < 8; ++index)
    {
        REQUIRE(harness.bus
                    .execute(std::make_unique<PlacePattern>(
                        PlacementId::generate(), patternId, 4.0 * static_cast<double>(index)))
                    .ok());
    }

    REQUIRE(harness.state.placementsOf(patternId).size() == 8);

    // One note, one command, one history entry — and the eight placements know
    // it, because not one of them holds a note.
    const auto before = harness.bus.undoDepth();
    REQUIRE(
        harness.bus.execute(std::make_unique<AddNote>(clipId, makeNote(NoteId::generate(), 36, 0.0))).ok());
    CHECK(harness.bus.undoDepth() == before + 1);

    CHECK(harness.state.findClip(clipId)->notes.size() == 1);
    CHECK(harness.state.placementsOf(patternId).size() == 8);

    // A ninth laying, made after the edit, carries it without a command of its
    // own. That is the whole reason content and position were split apart.
    REQUIRE(
        harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 32.0)).ok());
    CHECK(harness.state.placementsOf(patternId).size() == 9);
    CHECK(harness.state.findClip(clipId)->notes.size() == 1);
}

TEST_CASE("a pattern holds at most one row per track")
{
    Harness harness;
    const auto patternId = PatternId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<AddPatternTrack>(patternId, ClipId::generate(), harness.trackId))
            .ok());

    CHECK(
        harness.bus.execute(std::make_unique<AddPatternTrack>(patternId, ClipId::generate(), harness.trackId))
            .code() == ErrorCode::conflict);
}

TEST_CASE("shortening a pattern keeps the notes past its end")
{
    Harness harness;
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 4.0)).ok());

    const auto patternId = ProjectState::patternIdForClip(clipId);
    REQUIRE(
        harness.bus.execute(std::make_unique<AddNote>(clipId, makeNote(NoteId::generate(), 60, 3.5))).ok());

    REQUIRE(harness.bus.execute(std::make_unique<SetPatternLength>(patternId, 2.0)).ok());

    // The note is still there. A command that cut it could not give it back,
    // and an undo that gives back less than it took is not an undo.
    CHECK(harness.state.findPattern(patternId)->lengthBeats == doctest::Approx(2.0));
    CHECK(harness.state.findClip(clipId)->notes.size() == 1);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findPattern(patternId)->lengthBeats == doctest::Approx(4.0));
}

TEST_CASE("removing a track empties its rows, and undoing fills them back in")
{
    Harness harness;
    const auto patternId = PatternId::generate();
    const auto clipId = ClipId::generate();
    const auto noteId = NoteId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddPatternTrack>(patternId, clipId, harness.trackId)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clipId, makeNote(noteId, 42, 1.0))).ok());

    const auto before = harness.state.toValue();

    REQUIRE(harness.bus.execute(std::make_unique<RemoveTrack>(harness.trackId)).ok());

    // The pattern stays — it is not owned by a track — and it is empty.
    REQUIRE(harness.state.findPattern(patternId) != nullptr);
    CHECK(harness.state.findPattern(patternId)->clips.empty());
    CHECK(harness.state.findClip(clipId) == nullptr);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);
    REQUIRE(harness.state.findClip(clipId) != nullptr);
    CHECK(harness.state.findClip(clipId)->notes.front().id == noteId);
}

TEST_CASE("a project written before patterns existed reloads as patterns")
{
    // The shape a state had until the S9 model: the clips inside the track,
    // each with its own start and length. Nothing writes this any more, and it
    // is the shape every journal and every saved project of the first eight
    // weeks holds.
    const auto trackId = TrackId::generate();
    const auto clipId = ClipId::generate();
    const auto noteId = NoteId::generate();

    auto legacy = Value::object(
        {{"tempo",
          Value::array({Value::object({{"id", Value{ProjectState::originTempoPointId().toString()}},
                                       {"startBeats", Value{0.0}},
                                       {"beatsPerMinute", Value{120.0}}})})},
         {"tracks",
          Value::array(
              {Value::object({{"id", Value{trackId.toString()}},
                              {"name", Value{std::string{"Kick"}}},
                              {"volumeDb", Value{-3.0}},
                              {"clips",
                               Value::array({Value::object(
                                   {{"id", Value{clipId.toString()}},
                                    {"startBeats", Value{8.0}},
                                    {"lengthBeats", Value{4.0}},
                                    {"notes", Value::array({makeNote(noteId, 36, 0.0).toValue()})}})})},
                              {"plugins", Value::array({})}})})}});

    auto state = ProjectState::fromValue(legacy);
    REQUIRE(state.ok());

    const auto patternId = ProjectState::patternIdForClip(clipId);
    const auto* pattern = state.value().findPattern(patternId);
    REQUIRE(pattern != nullptr);
    CHECK(pattern->lengthBeats == doctest::Approx(4.0));
    REQUIRE(pattern->clips.size() == 1);
    CHECK(pattern->clips.front().trackId == trackId);
    CHECK(pattern->clips.front().notes.front().id == noteId);

    REQUIRE(state.value().placementsOf(patternId).size() == 1);
    CHECK(state.value().placementsOf(patternId).front()->startBeats == doctest::Approx(8.0));

    // And it is the same project as one built today by the command that wrote
    // those rows, which is what makes the migration invisible to the suite.
    ProjectState built;
    Track track{};
    track.id = trackId;
    track.name = "Kick";
    track.volumeDb = -3.0;
    REQUIRE(built.addTrack(track).ok());
    REQUIRE(built.addSingleTrackPattern(trackId, clipId, 8.0, 4.0).ok());
    REQUIRE(built.addNote(clipId, makeNote(noteId, 36, 0.0)).ok());
    CHECK(state.value() == built);
}

TEST_CASE("an undo record of track.remove written before patterns still reverts")
{
    // The record track.remove wrote until the S9 model: the whole track, clips
    // inside. Replaying an undo of that age must rebuild the same patterns the
    // redo of clip.create_midi would.
    Harness harness;
    const auto clipId = ClipId::generate();
    const auto noteId = NoteId::generate();

    auto legacyTrack = Value::object(
        {{"id", Value{harness.trackId.toString()}},
         {"name", Value{std::string{"Piste 1"}}},
         {"volumeDb", Value{0.0}},
         {"clips",
          Value::array({Value::object({{"id", Value{clipId.toString()}},
                                       {"startBeats", Value{0.0}},
                                       {"lengthBeats", Value{4.0}},
                                       {"notes", Value::array({makeNote(noteId, 36, 0.0).toValue()})}})})},
         {"plugins", Value::array({})}});

    const RemoveTrack command{harness.trackId};
    REQUIRE(harness.state.removeTrack(harness.trackId).ok());

    auto record = Value::object({{"index", Value{std::int64_t{0}}}, {"track", legacyTrack}});
    REQUIRE(command.revert(harness.state, record).ok());

    REQUIRE(harness.state.findTrack(harness.trackId) != nullptr);
    const auto* clip = harness.state.findClip(clipId);
    REQUIRE(clip != nullptr);
    CHECK(clip->trackId == harness.trackId);
    CHECK(clip->notes.front().id == noteId);
    CHECK(harness.state.placementsOf(ProjectState::patternIdForClip(clipId)).size() == 1);
}

TEST_CASE("the channel pitch is a property of the track, and it undoes")
{
    Harness harness;
    CHECK(harness.state.trackChannelPitch(harness.trackId).value() == 60);

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackChannelPitch>(harness.trackId, 36)).ok());
    CHECK(harness.state.trackChannelPitch(harness.trackId).value() == 36);

    CHECK(harness.bus.execute(std::make_unique<SetTrackChannelPitch>(harness.trackId, 200)).code() ==
          ErrorCode::invalidArgument);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.trackChannelPitch(harness.trackId).value() == 60);
}

TEST_CASE("dragging a channel pitch is one history entry")
{
    Harness harness;
    const auto gesture = harness.bus.beginGesture("hauteur du canal");

    ExecuteOptions options{};
    options.gesture = gesture;

    for (int pitch = 60; pitch >= 36; --pitch)
    {
        REQUIRE(harness.bus.execute(std::make_unique<SetTrackChannelPitch>(harness.trackId, pitch), options)
                    .ok());
    }

    CHECK(harness.bus.undoDepth() == 1);
    CHECK(harness.state.trackChannelPitch(harness.trackId).value() == 36);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.trackChannelPitch(harness.trackId).value() == 60);
}

TEST_CASE("a pattern and its placements survive a serialization round-trip")
{
    Harness harness;
    const auto patternId = PatternId::generate();
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 8.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddPatternTrack>(patternId, clipId, harness.trackId)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<AddNote>(clipId, makeNote(NoteId::generate(), 36, 0.0))).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 0.0)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 8.0)).ok());

    const auto restored = ProjectState::fromValue(harness.state.toValue());
    REQUIRE(restored.ok());
    CHECK(restored.value() == harness.state);
}

namespace
{

// A pattern of one row laid at each of the given beats. Returns the
// placements in the order they were laid.
struct Laid
{
    PatternId patternId{PatternId::generate()};
    ClipId clipId{ClipId::generate()};
    std::vector<PlacementId> placements;
};

Laid layPattern(Harness& harness, std::initializer_list<double> beats)
{
    Laid laid{};
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(laid.patternId, "Beat", 4.0)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<AddPatternTrack>(laid.patternId, laid.clipId, harness.trackId))
            .ok());

    for (const auto beat : beats)
    {
        laid.placements.push_back(PlacementId::generate());
        REQUIRE(
            harness.bus.execute(std::make_unique<PlacePattern>(laid.placements.back(), laid.patternId, beat))
                .ok());
    }
    return laid;
}

} // namespace

TEST_CASE("moving a placement moves that laying only, and a drag is one entry")
{
    Harness harness;
    const auto laid = layPattern(harness, {0.0, 4.0, 8.0});

    const auto gesture = harness.bus.beginGesture("déplacer un placement");
    ExecuteOptions options{};
    options.gesture = gesture;

    const auto before = harness.bus.undoDepth();
    for (double beat = 4.0; beat <= 16.0; beat += 1.0)
        REQUIRE(harness.bus.execute(std::make_unique<MovePlacement>(laid.placements[1], beat), options).ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == before + 1);
    CHECK(harness.state.findPlacement(laid.placements[1])->startBeats == doctest::Approx(16.0));

    // The two other layings of the same pattern did not move: a placement is a
    // position, and a position is not shared.
    CHECK(harness.state.findPlacement(laid.placements[0])->startBeats == doctest::Approx(0.0));
    CHECK(harness.state.findPlacement(laid.placements[2])->startBeats == doctest::Approx(8.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findPlacement(laid.placements[1])->startBeats == doctest::Approx(4.0));
}

TEST_CASE("a placement cannot be moved before the timeline origin")
{
    Harness harness;
    const auto laid = layPattern(harness, {4.0});

    CHECK(harness.bus.execute(std::make_unique<MovePlacement>(laid.placements[0], -1.0)).code() ==
          ErrorCode::invalidArgument);
    CHECK(harness.state.findPlacement(laid.placements[0])->startBeats == doctest::Approx(4.0));
}

TEST_CASE("removing a placement keeps the pattern, and undoing puts it back at its rank")
{
    Harness harness;
    const auto laid = layPattern(harness, {0.0, 4.0, 8.0});
    const auto original = harness.state.toValue();

    REQUIRE(harness.bus.execute(std::make_unique<RemovePlacement>(laid.placements[1])).ok());
    CHECK(harness.state.findPlacement(laid.placements[1]) == nullptr);
    CHECK(harness.state.findPattern(laid.patternId) != nullptr);
    CHECK(harness.state.placementsOf(laid.patternId).size() == 2);

    // Back where it was in the arrangement, not appended: two projects that
    // differ by the order of a vector are two serialized forms.
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == original);
}

TEST_CASE("renaming a pattern undoes to its previous name")
{
    Harness harness;
    const auto laid = layPattern(harness, {0.0});

    REQUIRE(harness.bus.execute(std::make_unique<RenamePattern>(laid.patternId, "Couplet")).ok());
    CHECK(harness.state.findPattern(laid.patternId)->name == "Couplet");

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findPattern(laid.patternId)->name == "Beat");
}

TEST_CASE("removing a pattern takes its rows and placements, and undoing restores the same arrangement")
{
    Harness harness;
    const auto first = layPattern(harness, {0.0, 8.0});
    const auto second = layPattern(harness, {4.0, 12.0});
    const auto third = layPattern(harness, {16.0});

    REQUIRE(
        harness.bus.execute(std::make_unique<AddNote>(second.clipId, makeNote(NoteId::generate(), 38, 1.0)))
            .ok());

    const auto original = harness.state.toValue();

    REQUIRE(harness.bus.execute(std::make_unique<RemovePattern>(second.patternId)).ok());
    CHECK(harness.state.findPattern(second.patternId) == nullptr);
    CHECK(harness.state.findClip(second.clipId) == nullptr);
    CHECK(harness.state.arrangement().size() == 3);

    // The other patterns and their layings are untouched.
    CHECK(harness.state.placementsOf(first.patternId).size() == 2);
    CHECK(harness.state.placementsOf(third.patternId).size() == 1);

    // Pattern at its rank, row with its note, placements interleaved exactly
    // where they were among the others.
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == original);

    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.findPattern(second.patternId) == nullptr);
}

TEST_CASE("the four arrangement verbs replay from their payloads")
{
    Harness harness;
    const auto laid = layPattern(harness, {0.0, 4.0});

    const std::vector<std::shared_ptr<Command>> commands{
        std::make_shared<RenamePattern>(laid.patternId, "Refrain"),
        std::make_shared<MovePlacement>(laid.placements[0], 2.0),
        std::make_shared<RemovePlacement>(laid.placements[1]),
        std::make_shared<RemovePattern>(laid.patternId)};

    for (const auto& command : commands)
    {
        auto rebuilt = harness.registry.create(command->type(), command->payload());
        REQUIRE(rebuilt.ok());
        CHECK(rebuilt.value()->payload() == command->payload());
    }
}
