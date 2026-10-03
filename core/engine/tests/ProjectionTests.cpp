#include "EngineTestSupport.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

TEST_CASE("A domain track becomes a Tracktion track, and nothing else remains")
{
    EngineHarness harness;

    const auto tracks = tracktion::getAudioTracks(harness.host.edit());

    // Tracktion creates one audio track with a new Edit. It carries no domain
    // identifier, so the projector removed it: the domain decides what exists.
    // The domain track is two: its strip, and the track that plays its notes
    // into it (S21); the strip comes first.
    REQUIRE(tracks.size() == 2);
    CHECK(tracks.getFirst()->getName() == "Piste 1");
}

TEST_CASE("A MIDI clip and its notes reach the Edit")
{
    EngineHarness harness;
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 4.0)).ok());

    auto* track = harness.notesTrack();
    REQUIRE(track != nullptr);
    REQUIRE(track->getClips().size() == 1);

    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 60)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 64)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 67)).ok());

    const auto clips = tracktion::getClipsOfType<tracktion::MidiClip>(*track);
    REQUIRE(clips.size() == 1);
    CHECK(clips.getFirst()->getSequence().getNumNotes() == 3);
}

TEST_CASE("Undo is projected like any other change, with no engine code of its own")
{
    EngineHarness harness;
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 60)).ok());

    auto* track = harness.notesTrack();
    REQUIRE(track != nullptr);

    REQUIRE(harness.bus.undo().ok()); // note
    CHECK(tracktion::getClipsOfType<tracktion::MidiClip>(*track).getFirst()->getSequence().getNumNotes() ==
          0);

    REQUIRE(harness.bus.undo().ok()); // clip
    CHECK(track->getClips().isEmpty());

    REQUIRE(harness.bus.redo().ok());
    CHECK(track->getClips().size() == 1);
}

TEST_CASE("A volume change reaches the track's volume plugin")
{
    EngineHarness harness;

    REQUIRE(harness.bus.execute(harness.setVolume(-6.0)).ok());

    auto* track = harness.stripTrack();
    REQUIRE(track != nullptr);

    auto* volume = track->getVolumePlugin();
    REQUIRE(volume != nullptr);
    CHECK(volume->getVolumeDb() == doctest::Approx(-6.0f).epsilon(0.01));
}

TEST_CASE("A coalesced drag leaves the clips alone")
{
    EngineHarness harness;
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 60)).ok());

    auto* track = harness.notesTrack();
    REQUIRE(track != nullptr);
    auto* clipBefore = track->getClips().getFirst();

    const auto gesture = harness.bus.beginGesture("fader");
    for (int frame = 0; frame < 30; ++frame)
        REQUIRE(
            harness.bus.execute(harness.setVolume(-0.1 * static_cast<double>(frame)), ExecuteOptions{gesture})
                .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    // The clip object is the same one: only the volume changed, so the
    // projector did not rebuild anything.
    REQUIRE(track->getClips().size() == 1);
    CHECK(track->getClips().getFirst() == clipBefore);
}

TEST_CASE("Reconciling twice changes nothing the second time")
{
    EngineHarness harness;
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(harness.createClip(clipId)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 60)).ok());

    auto* track = harness.notesTrack();
    REQUIRE(track != nullptr);
    auto* clipBefore = track->getClips().getFirst();

    harness.projector.reconcile();
    harness.projector.reconcile();

    REQUIRE(tracktion::getAudioTracks(harness.host.edit()).size() == 2);
    REQUIRE(track->getClips().size() == 1);
    CHECK(track->getClips().getFirst() == clipBefore);
}

TEST_CASE("Removing a track in the middle does not shift the others")
{
    EngineHarness harness;

    const auto second = TrackId::generate();
    const auto third = TrackId::generate();

    Track middle{};
    middle.id = second;
    middle.name = "Piste 2";
    REQUIRE(harness.state.addTrack(middle).ok());

    Track last{};
    last.id = third;
    last.name = "Piste 3";
    REQUIRE(harness.state.addTrack(last).ok());
    harness.projector.reconcile();

    REQUIRE(tracktion::getAudioTracks(harness.host.edit()).size() == 6);

    REQUIRE(harness.state.removeTrack(second).ok());
    harness.projector.reconcile();

    const auto tracks = tracktion::getAudioTracks(harness.host.edit());
    REQUIRE(tracks.size() == 4);

    // Binding is by identity, so the survivors keep their own names.
    juce::StringArray names;
    for (auto* track : tracks)
        names.add(track->getName());

    CHECK(names.contains("Piste 1"));
    CHECK(names.contains("Piste 3"));
    CHECK_FALSE(names.contains("Piste 2"));
}

TEST_CASE("Every projected track carries an instrument, so a note can be heard")
{
    EngineHarness harness;

    auto* track = harness.notesTrack();
    REQUIRE(track != nullptr);
    CHECK_FALSE(track->pluginList.getPluginsOfType<tracktion::FourOscPlugin>().isEmpty());
}

TEST_CASE("The tempo of the domain is the tempo of the Edit")
{
    EngineHarness harness;

    REQUIRE(harness.state.setTempoPointBpm(ProjectState::originTempoPointId(), 93.0).ok());
    harness.projector.reconcile();

    CHECK(harness.host.edit().tempoSequence.getTempo(0)->getBpm() == doctest::Approx(93.0));
}
