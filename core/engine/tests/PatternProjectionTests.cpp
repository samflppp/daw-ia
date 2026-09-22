#include "EngineTestSupport.h"
#include "daw/domain/commands/PatternCommands.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

namespace
{

// A pattern of one row on the harness's track, with one note in it.
struct Built
{
    PatternId patternId{PatternId::generate()};
    ClipId clipId{ClipId::generate()};
};

[[nodiscard]] Built buildPattern(EngineHarness& harness, double lengthBeats = 4.0)
{
    Built built{};
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(built.patternId, "Beat", lengthBeats)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<AddPatternTrack>(built.patternId, built.clipId, harness.trackId))
            .ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(built.clipId, NoteId::generate(), 36)).ok());
    return built;
}

} // namespace

TEST_CASE("A pattern laid eight times becomes eight clips in the Edit")
{
    EngineHarness harness;
    const auto built = buildPattern(harness);

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    // Not laid down yet: written, and silent. A pattern with no placement has
    // no position, so there is nothing for the Edit to hold.
    CHECK(track->getClips().isEmpty());

    for (int index = 0; index < 8; ++index)
    {
        REQUIRE(harness.bus
                    .execute(std::make_unique<PlacePattern>(
                        PlacementId::generate(), built.patternId, 4.0 * static_cast<double>(index)))
                    .ok());
    }

    const auto clips = tracktion::getClipsOfType<tracktion::MidiClip>(*track);
    REQUIRE(clips.size() == 8);

    for (auto* clip : clips)
        CHECK(clip->getSequence().getNumNotes() == 1);
}

TEST_CASE("One note added to a pattern reaches its eight placements")
{
    EngineHarness harness;
    const auto built = buildPattern(harness);

    for (int index = 0; index < 8; ++index)
    {
        REQUIRE(harness.bus
                    .execute(std::make_unique<PlacePattern>(
                        PlacementId::generate(), built.patternId, 4.0 * static_cast<double>(index)))
                    .ok());
    }

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    // One command, and the eight clips hear it. That is the whole reason
    // content and position were split: the domain holds the note once.
    REQUIRE(harness.bus.execute(EngineHarness::addNote(built.clipId, NoteId::generate(), 42)).ok());

    const auto clips = tracktion::getClipsOfType<tracktion::MidiClip>(*track);
    REQUIRE(clips.size() == 8);
    for (auto* clip : clips)
        CHECK(clip->getSequence().getNumNotes() == 2);

    // And undoing it takes it out of the eight, with no engine code of its own.
    REQUIRE(harness.bus.undo().ok());
    for (auto* clip : tracktion::getClipsOfType<tracktion::MidiClip>(*track))
        CHECK(clip->getSequence().getNumNotes() == 1);
}

TEST_CASE("A placement lands where it says, in seconds the tempo decides")
{
    EngineHarness harness;
    const auto built = buildPattern(harness, 4.0);

    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), built.patternId, 8.0))
                .ok());

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    const auto clips = tracktion::getClipsOfType<tracktion::MidiClip>(*track);
    REQUIRE(clips.size() == 1);

    // Eight beats at 120 BPM is four seconds, and the clip is as long as the
    // pattern: two beats' worth of length would mean the pattern's length had
    // been read from somewhere else.
    CHECK(clips.getFirst()->getPosition().getStart().inSeconds() == doctest::Approx(4.0));
    CHECK(clips.getFirst()->getPosition().getLength().inSeconds() == doctest::Approx(2.0));
}

TEST_CASE("Changing a pattern's length moves what every placement covers")
{
    EngineHarness harness;
    const auto built = buildPattern(harness, 4.0);

    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), built.patternId, 0.0))
                .ok());

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    REQUIRE(harness.bus.execute(std::make_unique<SetPatternLength>(built.patternId, 8.0)).ok());

    const auto clips = tracktion::getClipsOfType<tracktion::MidiClip>(*track);
    REQUIRE(clips.size() == 1);
    CHECK(clips.getFirst()->getPosition().getLength().inSeconds() == doctest::Approx(4.0));
}

TEST_CASE("A legacy clip.create_midi still puts one clip where it always did")
{
    // The command whose payload eight weeks of journals hold. It means a
    // pattern and a placement now, and what reaches the Edit has to be what it
    // always was: one clip, at that beat, of that length.
    EngineHarness harness;
    const auto clipId = ClipId::generate();

    REQUIRE(harness.bus.execute(harness.createClip(clipId, 8.0, 4.0)).ok());
    REQUIRE(harness.bus.execute(EngineHarness::addNote(clipId, NoteId::generate(), 60)).ok());

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    const auto clips = tracktion::getClipsOfType<tracktion::MidiClip>(*track);
    REQUIRE(clips.size() == 1);
    CHECK(clips.getFirst()->getPosition().getStart().inSeconds() == doctest::Approx(4.0));
    CHECK(clips.getFirst()->getPosition().getLength().inSeconds() == doctest::Approx(2.0));
    CHECK(clips.getFirst()->getSequence().getNumNotes() == 1);
}
