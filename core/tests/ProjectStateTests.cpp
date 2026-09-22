#include "daw/domain/project/ProjectState.h"

#include <doctest/doctest.h>

using namespace daw::domain;

namespace
{

Track makeTrack(TrackId id)
{
    Track track{};
    track.id = id;
    track.name = "Piste";
    track.volumeDb = -3.0;
    return track;
}

Clip makeClip(ClipId id, TrackId trackId)
{
    Clip clip{};
    clip.id = id;
    clip.trackId = trackId;
    return clip;
}

// What a clip used to be: a track, a start and a length. It is now a pattern of
// one row plus a placement, which is exactly what clip.create_midi builds, so
// the tests keep reading as they did.
Result<void> addClipOnTrack(ProjectState& state, TrackId trackId, ClipId clipId, double start = 0.0)
{
    return state.addSingleTrackPattern(trackId, clipId, start, 4.0);
}

Note makeNote(NoteId id)
{
    Note note{};
    note.id = id;
    note.pitch = 60;
    note.velocity = 100;
    note.startBeats = 1.0;
    note.lengthBeats = 0.5;
    return note;
}

} // namespace

TEST_CASE("A refused mutation changes nothing")
{
    ProjectState state;
    const auto trackId = TrackId::generate();
    REQUIRE(state.addTrack(makeTrack(trackId)).ok());

    const auto before = state.toValue();

    SUBCASE("unknown track")
    {
        CHECK(state.setTrackVolume(TrackId::generate(), -6.0).code() == ErrorCode::notFound);
    }

    SUBCASE("volume out of range")
    {
        CHECK(state.setTrackVolume(trackId, 40.0).code() == ErrorCode::invalidArgument);
    }

    SUBCASE("duplicate track")
    {
        CHECK(state.addTrack(makeTrack(trackId)).code() == ErrorCode::conflict);
    }

    SUBCASE("clip on an unknown track")
    {
        CHECK(addClipOnTrack(state, TrackId::generate(), ClipId::generate()).code() == ErrorCode::notFound);
    }

    SUBCASE("a row in a pattern that does not exist")
    {
        CHECK(state.addClip(PatternId::generate(), makeClip(ClipId::generate(), trackId)).code() ==
              ErrorCode::notFound);
    }

    SUBCASE("a placement of a pattern that does not exist")
    {
        Placement placement{};
        placement.id = PlacementId::generate();
        placement.patternId = PatternId::generate();
        CHECK(state.addPlacement(placement).code() == ErrorCode::notFound);
    }

    SUBCASE("note with an impossible pitch")
    {
        const auto clipId = ClipId::generate();
        REQUIRE(addClipOnTrack(state, trackId, clipId).ok());

        auto note = makeNote(NoteId::generate());
        note.pitch = 200;
        CHECK(state.addNote(clipId, note).code() == ErrorCode::invalidArgument);

        // The pattern added by this subcase is the only expected difference,
        // and removing it takes its placement and its row with it.
        REQUIRE(state.removePattern(ProjectState::patternIdForClip(clipId)).ok());
    }

    CHECK(state.toValue() == before);
}

TEST_CASE("Nested lookups find clips and notes across tracks")
{
    ProjectState state;
    const auto firstTrack = TrackId::generate();
    const auto secondTrack = TrackId::generate();
    REQUIRE(state.addTrack(makeTrack(firstTrack)).ok());
    REQUIRE(state.addTrack(makeTrack(secondTrack)).ok());

    const auto clipId = ClipId::generate();
    REQUIRE(addClipOnTrack(state, secondTrack, clipId).ok());

    const auto noteId = NoteId::generate();
    REQUIRE(state.addNote(clipId, makeNote(noteId)).ok());

    const auto* clip = state.findClip(clipId);
    REQUIRE(clip != nullptr);
    REQUIRE(clip->notes.size() == 1);
    CHECK(clip->notes.front().id == noteId);

    CHECK(state.removeNote(clipId, NoteId::generate()).code() == ErrorCode::notFound);
    REQUIRE(state.removeNote(clipId, noteId).ok());
    CHECK(state.findClip(clipId)->notes.empty());
}

namespace
{

TempoPoint makeTempoPoint(TempoPointId id, double startBeats, double beatsPerMinute)
{
    TempoPoint point{};
    point.id = id;
    point.startBeats = startBeats;
    point.beatsPerMinute = beatsPerMinute;
    return point;
}

} // namespace

TEST_CASE("An empty project already holds a tempo sequence, and two of them are equal")
{
    const ProjectState first;
    const ProjectState second;

    REQUIRE(first.tempoPoints().size() == 1);
    CHECK(first.tempoPoints().front().id == ProjectState::originTempoPointId());
    CHECK(first.tempoPoints().front().startBeats == 0.0);
    CHECK(first.tempoAt(0.0) == doctest::Approx(120.0));

    // The origin point is not generated, so two empty projects are the same
    // project. A generated identifier would already make them differ here.
    CHECK(first == second);
}

TEST_CASE("The tempo in force is the last point at or before the beat")
{
    ProjectState state;
    const auto second = TempoPointId::generate();
    const auto third = TempoPointId::generate();

    REQUIRE(state.insertTempoPoint(makeTempoPoint(third, 16.0, 90.0)).ok());
    REQUIRE(state.insertTempoPoint(makeTempoPoint(second, 8.0, 140.0)).ok());

    // Inserted out of order, kept in order.
    REQUIRE(state.tempoPoints().size() == 3);
    CHECK(state.tempoPoints()[1].id == second);
    CHECK(state.tempoPoints()[2].id == third);

    CHECK(state.tempoAt(0.0) == doctest::Approx(120.0));
    CHECK(state.tempoAt(7.99) == doctest::Approx(120.0));
    CHECK(state.tempoAt(8.0) == doctest::Approx(140.0));
    CHECK(state.tempoAt(15.5) == doctest::Approx(140.0));
    CHECK(state.tempoAt(1000.0) == doctest::Approx(90.0));
}

TEST_CASE("The sequence keeps its invariants, and says which one was broken")
{
    ProjectState state;
    const auto point = TempoPointId::generate();
    REQUIRE(state.insertTempoPoint(makeTempoPoint(point, 8.0, 140.0)).ok());

    SUBCASE("two points cannot share a beat")
    {
        CHECK(state.insertTempoPoint(makeTempoPoint(TempoPointId::generate(), 8.0, 90.0)).code() ==
              ErrorCode::conflict);
        CHECK(state.moveTempoPoint(point, 0.0).code() == ErrorCode::invalidArgument);
    }

    SUBCASE("the same identifier cannot be inserted twice")
    {
        CHECK(state.insertTempoPoint(makeTempoPoint(point, 12.0, 90.0)).code() == ErrorCode::conflict);
    }

    SUBCASE("the origin point stays, and stays at the origin")
    {
        CHECK(state.removeTempoPoint(ProjectState::originTempoPointId()).code() ==
              ErrorCode::invalidArgument);
        CHECK(state.moveTempoPoint(ProjectState::originTempoPointId(), 4.0).code() ==
              ErrorCode::invalidArgument);
        CHECK(state.tempoPoints().front().id == ProjectState::originTempoPointId());
    }

    SUBCASE("the bounds are Tracktion's own")
    {
        CHECK(state.setTempoPointBpm(point, 19.9).code() == ErrorCode::invalidArgument);
        CHECK(state.setTempoPointBpm(point, 300.1).code() == ErrorCode::invalidArgument);
        REQUIRE(state.setTempoPointBpm(point, 300.0).ok());
        CHECK(state.tempoAt(8.0) == doctest::Approx(300.0));
    }

    SUBCASE("an unknown point is not found")
    {
        const auto absent = TempoPointId::generate();
        CHECK(state.setTempoPointBpm(absent, 90.0).code() == ErrorCode::notFound);
        CHECK(state.moveTempoPoint(absent, 4.0).code() == ErrorCode::notFound);
        CHECK(state.removeTempoPoint(absent).code() == ErrorCode::notFound);
    }

    // Every refusal above left the sequence exactly as it was.
    CHECK(state.tempoPoints().size() == 2);
}

TEST_CASE("Moving a point re-sorts the sequence")
{
    ProjectState state;
    const auto early = TempoPointId::generate();
    const auto late = TempoPointId::generate();

    REQUIRE(state.insertTempoPoint(makeTempoPoint(early, 4.0, 140.0)).ok());
    REQUIRE(state.insertTempoPoint(makeTempoPoint(late, 8.0, 90.0)).ok());

    REQUIRE(state.moveTempoPoint(early, 12.0).ok());
    CHECK(state.tempoPoints()[1].id == late);
    CHECK(state.tempoPoints()[2].id == early);
    CHECK(state.tempoAt(9.0) == doctest::Approx(90.0));
    CHECK(state.tempoAt(12.0) == doctest::Approx(140.0));
}

TEST_CASE("A project written with a scalar tempo reads as a sequence of one point")
{
    // Exactly what S5 and S6 wrote on disk. Nothing rewrites it: it is read.
    const auto legacy = Value::object({{"tempo", Value{93.0}}, {"tracks", Value::array({})}});

    const auto state = ProjectState::fromValue(legacy);
    REQUIRE(state.ok());
    REQUIRE(state.value().tempoPoints().size() == 1);
    CHECK(state.value().tempoPoints().front().id == ProjectState::originTempoPointId());
    CHECK(state.value().tempoAt(0.0) == doctest::Approx(93.0));

    // And it is the same project as one built today at that tempo, which is
    // what makes the migration invisible to every comparison in the suite.
    ProjectState built;
    REQUIRE(built.setTempoPointBpm(ProjectState::originTempoPointId(), 93.0).ok());
    CHECK(state.value() == built);
}

TEST_CASE("A tempo sequence survives a serialization round-trip")
{
    ProjectState state;
    REQUIRE(state.setTempoPointBpm(ProjectState::originTempoPointId(), 93.0).ok());
    REQUIRE(state.insertTempoPoint(makeTempoPoint(TempoPointId::generate(), 8.0, 140.0)).ok());
    REQUIRE(state.insertTempoPoint(makeTempoPoint(TempoPointId::generate(), 16.0, 60.0)).ok());

    const auto restored = ProjectState::fromValue(state.toValue());
    REQUIRE(restored.ok());
    CHECK(restored.value() == state);
}

TEST_CASE("A tempo sequence without its origin point is refused")
{
    const auto orphan = Value::object(
        {{"tempo", Value::array({makeTempoPoint(TempoPointId::generate(), 4.0, 140.0).toValue()})},
         {"tracks", Value::array({})}});

    CHECK(ProjectState::fromValue(orphan).error().code == ErrorCode::invalidPayload);
}

TEST_CASE("A project survives a serialization round-trip")
{
    ProjectState state;
    REQUIRE(state.setTempoPointBpm(ProjectState::originTempoPointId(), 93.0).ok());

    const auto trackId = TrackId::generate();
    const auto clipId = ClipId::generate();
    REQUIRE(state.addTrack(makeTrack(trackId)).ok());
    REQUIRE(addClipOnTrack(state, trackId, clipId).ok());
    REQUIRE(state.addNote(clipId, makeNote(NoteId::generate())).ok());

    const auto restored = ProjectState::fromValue(state.toValue());
    REQUIRE(restored.ok());
    CHECK(restored.value() == state);
}

TEST_CASE("A malformed project is refused, not half-loaded")
{
    const auto missingTempo = ProjectState::fromValue(Value::object({{"tracks", Value::array({})}}));
    CHECK(missingTempo.error().code == ErrorCode::invalidPayload);

    const auto badTracks =
        ProjectState::fromValue(Value::object({{"tempo", Value{120.0}}, {"tracks", Value{3}}}));
    CHECK(badTracks.error().code == ErrorCode::invalidPayload);

    const auto badTempo =
        ProjectState::fromValue(Value::object({{"tempo", Value{5000.0}}, {"tracks", Value::array({})}}));
    CHECK(badTempo.error().code == ErrorCode::invalidArgument);
}
