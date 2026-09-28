#include "EngineTestSupport.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TempoCommands.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

// The playlist, proved against what the Edit holds and against what it sounds
// like.
//
// Reading ProjectState back proves that a number was stored. The claim of the
// pattern model is about sound — a pattern laid eight times and edited once
// changes in the eight places — so the proofs below render the Edit offline
// and listen to it: how long the render lasts, and in which beats something
// sounds.

namespace
{

constexpr double secondsPerBeatAt120 = 0.5;

struct Beat
{
    PatternId patternId{PatternId::generate()};
    ClipId clipId{ClipId::generate()};
    std::vector<PlacementId> placements;
};

Note hit(double startBeats)
{
    Note note{};
    note.id = NoteId::generate();
    note.pitch = 48;
    note.velocity = 110;
    note.startBeats = startBeats;
    note.lengthBeats = 0.25;
    return note;
}

// A four-beat pattern holding one short hit on its first beat, laid `times`
// times back to back from beat 0.
Beat layBeat(EngineHarness& harness, int times)
{
    Beat beat{};
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(beat.patternId, "Beat", 4.0)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<AddPatternTrack>(beat.patternId, beat.clipId, harness.trackId))
            .ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(beat.clipId, hit(0.0))).ok());

    for (int index = 0; index < times; ++index)
    {
        beat.placements.push_back(PlacementId::generate());
        REQUIRE(harness.bus
                    .execute(std::make_unique<PlacePattern>(
                        beat.placements.back(), beat.patternId, 4.0 * static_cast<double>(index)))
                    .ok());
    }
    return beat;
}

struct Listening
{
    double seconds{0.0};

    // One entry per beat of the render: whether something starts sounding
    // there, judged against the loudest beat of the same render.
    std::vector<bool> sounding;

    [[nodiscard]] int soundingCount() const
    {
        return static_cast<int>(std::count(sounding.begin(), sounding.end(), true));
    }
};

// Renders the Edit and listens to the first quarter of every beat.
//
// A hit is a quarter of a beat long, so its onset lives in that quarter; the
// release of 4OSC has decayed long before the next beat starts. The threshold
// is relative, so the test does not depend on the synth's level.
Listening listen(tracktion::Edit& edit)
{
    const auto rendered = tracktion::test_utilities::renderToAudioBuffer(edit);
    REQUIRE(rendered.buffer.getNumSamples() > 0);

    Listening heard{};
    heard.seconds = static_cast<double>(rendered.buffer.getNumSamples()) / rendered.sampleRate;

    const auto samplesPerBeat = static_cast<int>(std::lround(secondsPerBeatAt120 * rendered.sampleRate));
    const auto window = samplesPerBeat / 4;
    const auto beats = rendered.buffer.getNumSamples() / samplesPerBeat;

    std::vector<double> levels;
    for (int beat = 0; beat < beats; ++beat)
        levels.push_back(rendered.buffer.getRMSLevel(0, beat * samplesPerBeat, window));

    const auto loudest = levels.empty() ? 0.0 : *std::max_element(levels.begin(), levels.end());
    REQUIRE(loudest > 0.001);

    for (const auto level : levels)
        heard.sounding.push_back(level > loudest * 0.3);

    return heard;
}

void expectHitsEvery(const Listening& heard, int firstBeat, int period, int count)
{
    for (int index = 0; index < count; ++index)
    {
        const auto beat = static_cast<std::size_t>(firstBeat + index * period);
        REQUIRE(beat < heard.sounding.size());
        CHECK(heard.sounding[beat]);
    }
}

} // namespace

TEST_CASE("A pattern laid eight times, edited once, sounds changed in the eight places")
{
    EngineHarness harness;
    const auto beat = layBeat(harness, 8);

    // Eight layings of four beats: thirty-two beats, sixteen seconds at 120.
    auto before = listen(harness.host.edit());
    CHECK(before.seconds == doctest::Approx(16.0).epsilon(0.02));
    CHECK(before.soundingCount() == 8);
    expectHitsEvery(before, 0, 4, 8);

    // One note, one command, in the pattern — and the render hears it on the
    // third beat of every laying.
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(beat.clipId, hit(2.0))).ok());

    auto after = listen(harness.host.edit());
    MESSAGE("render before: " << before.seconds << " s, " << before.soundingCount()
                              << " beats sounding; after: " << after.seconds << " s, "
                              << after.soundingCount() << " beats sounding");
    CHECK(after.seconds == doctest::Approx(16.0).epsilon(0.02));
    CHECK(after.soundingCount() == 16);
    expectHitsEvery(after, 0, 4, 8);
    expectHitsEvery(after, 2, 4, 8);

    // One Ctrl+Z takes it out of the eight.
    REQUIRE(harness.bus.undo().ok());
    CHECK(listen(harness.host.edit()).soundingCount() == 8);
}

TEST_CASE("Moving and removing placements changes where and how long the song sounds")
{
    EngineHarness harness;
    const auto beat = layBeat(harness, 2);

    REQUIRE(harness.bus.execute(std::make_unique<MovePlacement>(beat.placements[1], 12.0)).ok());

    auto moved = listen(harness.host.edit());
    CHECK(moved.seconds == doctest::Approx(8.0).epsilon(0.02)); // ends at beat 16
    CHECK(moved.soundingCount() == 2);
    CHECK(moved.sounding[0]);
    CHECK(moved.sounding[12]);

    REQUIRE(harness.bus.execute(std::make_unique<RemovePlacement>(beat.placements[1])).ok());

    auto removed = listen(harness.host.edit());
    CHECK(removed.seconds == doctest::Approx(2.0).epsilon(0.02));
    CHECK(removed.soundingCount() == 1);
}

TEST_CASE("Pattern mode plays the auditioned pattern alone, once, from the start")
{
    EngineHarness harness;
    layBeat(harness, 4);

    // A second pattern laid under the first one's third laying: song mode
    // hears it, pattern mode on the first pattern must not.
    const auto other = PatternId::generate();
    const auto otherRow = ClipId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(other, "Fill", 4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddPatternTrack>(other, otherRow, harness.trackId)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(otherRow, hit(1.0))).ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), other, 8.0)).ok());

    auto song = listen(harness.host.edit());
    CHECK(song.soundingCount() == 5);
    CHECK(song.sounding[9]);

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, other)).ok());

    // The Edit now holds the fill and nothing else, at beat 0: four beats,
    // one hit on the second.
    auto pattern = listen(harness.host.edit());
    CHECK(pattern.seconds == doctest::Approx(2.0).epsilon(0.02));
    CHECK(pattern.soundingCount() == 1);
    CHECK(pattern.sounding[1]);

    auto& transport = harness.host.edit().getTransport();
    CHECK(transport.looping.get());
    CHECK(transport.getLoopRange().getStart().inSeconds() == doctest::Approx(0.0));
    CHECK(transport.getLoopRange().getEnd().inSeconds() == doctest::Approx(2.0));

    // Back to the song: the arrangement is laid down again, and the loop
    // pattern mode imposed is gone.
    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::song, PatternId{})).ok());
    CHECK(listen(harness.host.edit()).soundingCount() == 5);
    CHECK_FALSE(transport.looping.get());
}

TEST_CASE("The pattern-mode loop follows the pattern's length without a transport command")
{
    EngineHarness harness;
    const auto beat = layBeat(harness, 1);

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, beat.patternId)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetPatternLength>(beat.patternId, 8.0)).ok());

    const auto range = harness.host.edit().getTransport().getLoopRange();
    CHECK(range.getEnd().inSeconds() == doctest::Approx(4.0));
}

TEST_CASE("Moving one placement repositions one clip and rebuilds nothing")
{
    EngineHarness harness;
    const auto beat = layBeat(harness, 8);

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    std::vector<tracktion::EditItemID> before;
    for (auto* clip : track->getClips())
        before.push_back(clip->itemID);
    REQUIRE(before.size() == 8);

    const auto stats = harness.projector.stats();
    REQUIRE(harness.bus.execute(std::make_unique<MovePlacement>(beat.placements[3], 40.0)).ok());

    CHECK(harness.projector.stats().clipsInserted == stats.clipsInserted);
    CHECK(harness.projector.stats().clipsRewritten == stats.clipsRewritten);
    CHECK(harness.projector.stats().clipsMoved == stats.clipsMoved + 1);

    // The same eight Tracktion objects: identity, not a rebuild that happens
    // to look the same.
    std::vector<tracktion::EditItemID> after;
    for (auto* clip : track->getClips())
        after.push_back(clip->itemID);
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    CHECK(before == after);
}

TEST_CASE("Editing a pattern laid eight times rewrites eight sequences and inserts nothing")
{
    EngineHarness harness;
    const auto beat = layBeat(harness, 8);

    const auto stats = harness.projector.stats();
    REQUIRE(harness.bus.execute(std::make_unique<AddNote>(beat.clipId, hit(3.0))).ok());

    CHECK(harness.projector.stats().clipsInserted == stats.clipsInserted);
    CHECK(harness.projector.stats().clipsMoved == stats.clipsMoved);
    CHECK(harness.projector.stats().clipsRewritten == stats.clipsRewritten + 8);
}

TEST_CASE("A tempo change moves every clip in seconds and rewrites none")
{
    // The trap of S7: the Edit holds seconds, and a tempo change moves every
    // second while moving no beat. Forgetting it leaves the song playing at
    // the old tempo's positions.
    EngineHarness harness;
    const auto beat = layBeat(harness, 4);
    static_cast<void>(beat);

    const auto stats = harness.projector.stats();
    REQUIRE(harness.bus.execute(std::make_unique<SetTempoPointBpm>(ProjectState::originTempoPointId(), 60.0))
                .ok());

    CHECK(harness.projector.stats().clipsInserted == stats.clipsInserted);
    CHECK(harness.projector.stats().clipsRewritten == stats.clipsRewritten);
    CHECK(harness.projector.stats().clipsMoved == stats.clipsMoved + 4);

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    // Beat 12 at 60 BPM is twelve seconds; at 120 it was six.
    std::vector<double> starts;
    for (auto* clip : track->getClips())
        starts.push_back(clip->getPosition().getStart().inSeconds());
    std::sort(starts.begin(), starts.end());
    REQUIRE(starts.size() == 4);
    CHECK(starts[3] == doctest::Approx(12.0));
    CHECK(track->getClips().getFirst()->getPosition().getLength().inSeconds() == doctest::Approx(4.0));
}

TEST_CASE("Filing blocks on other lines touches no clip and sounds the same, beat by beat")
{
    // A line is where a block is filed, never what it plays: the S17 claim is
    // about sound, so it is proved on renders and on the Edit, clip for clip.
    //
    // Not sample for sample: two renders of an untouched Edit already differ
    // in their bits (4OSC runs free), so the proof is what the ear gets —
    // where each beat starts sounding, and how loud each beat is.
    EngineHarness harness;
    const auto beat = layBeat(harness, 4);

    const auto levels = [&harness]
    {
        const auto rendered = tracktion::test_utilities::renderToAudioBuffer(harness.host.edit());
        const auto samplesPerBeat = static_cast<int>(std::lround(secondsPerBeatAt120 * rendered.sampleRate));
        std::vector<double> perBeat;
        for (int start = 0; start + samplesPerBeat <= rendered.buffer.getNumSamples();
             start += samplesPerBeat)
            perBeat.push_back(rendered.buffer.getRMSLevel(0, start, samplesPerBeat));
        return perBeat;
    };

    const auto heardBefore = listen(harness.host.edit());
    const auto before = levels();

    const auto stats = harness.projector.stats();
    const auto verse = LaneId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(verse, "Couplet", 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<MovePlacement>(beat.placements[1], 4.0, verse)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<MovePlacement>(beat.placements[3], 12.0, verse)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<MoveLane>(verse, 5)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<RenameLane>(verse, "Refrain")).ok());

    CHECK(harness.projector.stats().clipsInserted == stats.clipsInserted);
    CHECK(harness.projector.stats().clipsRewritten == stats.clipsRewritten);
    CHECK(harness.projector.stats().clipsMoved == stats.clipsMoved);

    const auto heardAfter = listen(harness.host.edit());
    CHECK(heardAfter.sounding == heardBefore.sounding);
    CHECK(heardAfter.soundingCount() == 4);

    const auto after = levels();
    REQUIRE(after.size() == before.size());
    const auto loudest = *std::max_element(before.begin(), before.end());
    for (std::size_t index = 0; index < before.size(); ++index)
        CHECK(std::abs(after[index] - before[index]) <= loudest * 0.02);
}
