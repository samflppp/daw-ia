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

Clip makeClip(ClipId id)
{
    Clip clip{};
    clip.id = id;
    clip.startBeats = 0.0;
    clip.lengthBeats = 4.0;
    return clip;
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
        CHECK(state.addClip(TrackId::generate(), makeClip(ClipId::generate())).code() == ErrorCode::notFound);
    }

    SUBCASE("note with an impossible pitch")
    {
        const auto clipId = ClipId::generate();
        REQUIRE(state.addClip(trackId, makeClip(clipId)).ok());

        auto note = makeNote(NoteId::generate());
        note.pitch = 200;
        CHECK(state.addNote(clipId, note).code() == ErrorCode::invalidArgument);

        // The clip added by this subcase is the only expected difference.
        REQUIRE(state.removeClip(clipId).ok());
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
    REQUIRE(state.addClip(secondTrack, makeClip(clipId)).ok());

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

TEST_CASE("A project survives a serialization round-trip")
{
    ProjectState state;
    REQUIRE(state.setTempo(93.0).ok());

    const auto trackId = TrackId::generate();
    const auto clipId = ClipId::generate();
    REQUIRE(state.addTrack(makeTrack(trackId)).ok());
    REQUIRE(state.addClip(trackId, makeClip(clipId)).ok());
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
