#include "TestSupport.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/live/Take.h"
#include "daw/domain/serialization/Json.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::live;
using daw::testing::Harness;

// A take (S23): what was played while recording, written into the project in
// one group of existing commands, at the end. Played here as a keyboard
// plays: note-ons and releases on the input clock, with the song's position
// as the audio thread publishes it.

namespace
{

// 120 BPM: two beats a second.
double beatsAt(double seconds)
{
    return seconds * 2.0;
}

Event noteOn(int pitch, int velocity, double seconds)
{
    Event event;
    event.bytes = {0x90, static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(velocity)};
    event.size = 3;
    event.seconds = seconds;
    return event;
}

Event noteOff(int pitch, double seconds)
{
    Event event;
    event.bytes = {0x80, static_cast<std::uint8_t>(pitch), 64};
    event.size = 3;
    event.seconds = seconds;
    return event;
}

Event pedal(bool down, double seconds)
{
    Event event;
    event.bytes = {0xB0, 64, static_cast<std::uint8_t>(down ? 127 : 0)};
    event.size = 3;
    event.seconds = seconds;
    return event;
}

// The song started at 100 s on the input clock, at the start of the pattern.
constexpr Position playing{100.0, 0.0, true};

TakeTiming patternTiming(double latency = 0.0, double length = 16.0)
{
    TakeTiming timing;
    timing.latencySeconds = latency;
    timing.loopStartBeats = 0.0;
    timing.loopLengthBeats = length;
    timing.beatsAt = beatsAt;
    return timing;
}

// Identifiers as the caller gives them, numbered so that a run can be made
// again with the same ones.
struct Ids
{
    std::vector<NoteId> notes;
    std::vector<ClipId> rows;
    std::size_t nextNote{0};
    std::size_t nextRow{0};

    Ids()
    {
        for (int index = 0; index < 64; ++index)
            notes.push_back(NoteId::generate());
        for (int index = 0; index < 8; ++index)
            rows.push_back(ClipId::generate());
    }

    TakeIds take()
    {
        nextNote = 0;
        nextRow = 0;
        return {[this] { return notes.at(nextNote++); }, [this] { return rows.at(nextRow++); }};
    }
};

// A pattern of four bars, its row for the harness's track opened.
struct PatternRig
{
    PatternRig()
    {
        REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(pattern, std::string{}, 16.0)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<AddPatternTrack>(pattern, row, harness.trackId)).ok());
    }

    [[nodiscard]] const Clip& clip() const
    {
        return *harness.state.findPattern(pattern)->findClipForTrack(harness.trackId);
    }

    Harness harness;
    PatternId pattern{PatternId::generate()};
    ClipId row{ClipId::generate()};
};

bool executeTake(Harness& harness, std::vector<std::unique_ptr<Command>> commands)
{
    GroupOptions group;
    group.label = "prise";
    return harness.bus.executeGroup(std::move(commands), group).ok();
}

} // namespace

TEST_CASE("Take: the notes played become exactly those notes, in one group, undone to the byte")
{
    PatternRig rig;
    auto& harness = rig.harness;
    const auto before = json::write(harness.state.toValue());
    const auto depth = harness.bus.undoDepth();

    TakeBuilder take{patternTiming()};
    take.add(noteOn(60, 100, 100.5), harness.trackId, playing); // beat 1
    take.add(noteOn(64, 80, 101.25), harness.trackId, playing); // beat 2.5
    take.add(noteOff(60, 101.0), harness.trackId, playing);     // a beat long
    take.add(noteOff(64, 101.5), harness.trackId, playing);     // half a beat
    take.stop(102.0, playing);

    Ids ids;
    const auto destination = TakeDestination{PlayMode::pattern, rig.pattern, {}, 0.0, 0.0};
    REQUIRE(executeTake(harness,
                        takeCommands(harness.state, destination, take.notes(102.0, playing), ids.take())));

    const auto& notes = rig.clip().notes;
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].id == ids.notes[0]);
    CHECK(notes[0].pitch == 60);
    CHECK(notes[0].velocity == 100);
    CHECK(notes[0].startBeats == doctest::Approx(1.0));
    CHECK(notes[0].lengthBeats == doctest::Approx(1.0));
    CHECK(notes[1].pitch == 64);
    CHECK(notes[1].velocity == 80);
    CHECK(notes[1].startBeats == doctest::Approx(2.5));
    CHECK(notes[1].lengthBeats == doctest::Approx(0.5));
    CHECK(harness.bus.undoDepth() == depth + 1); // one entry in the history

    // Replayed from its journal into a fresh project: the same project.
    const auto after = json::write(harness.state.toValue());
    {
        ProjectState state;
        Track track{};
        track.id = harness.trackId;
        track.name = "Piste 1";
        REQUIRE(state.addTrack(track).ok());
        const auto registry = CommandRegistry::withBuiltinCommands();
        CommandBus replay{state, registry};
        for (const auto& envelope : harness.bus.journal())
            REQUIRE(replay.executeSerialized(envelope).ok());
        CHECK(json::write(state.toValue()) == after);
    }

    REQUIRE(harness.bus.undo().ok());
    CHECK(json::write(harness.state.toValue()) == before);
}

TEST_CASE("Take: a note heard on the beat is written on the beat, the card's latency taken back")
{
    // The card plays 30 ms after the engine renders: the key the person
    // presses on beat 4, as heard, comes when the engine is at 4 beats and
    // 30 ms.
    TakeBuilder take{patternTiming(0.030)};
    const auto pressed = 100.0 + 2.0 + 0.030;
    take.add(noteOn(62, 90, pressed), TrackId::generate(), playing);
    const auto notes = take.notes(pressed + 0.1, playing);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].open);
    CHECK(notes[0].startBeats == doctest::Approx(4.0).epsilon(1e-9));
    CHECK(take.beatsHeard(pressed, playing) == doctest::Approx(4.0));
}

TEST_CASE("Take: a pattern loops, a note just before its first beat falls at its end")
{
    // The song has just gone round the loop (0.02 s into the pattern); with
    // 50 ms of latency, the key was pressed on what was heard 30 ms before the
    // end of the pattern.
    TakeBuilder take{patternTiming(0.050)};
    const Position justAfter{200.0, 0.02, true};
    take.add(noteOn(60, 100, 200.0), TrackId::generate(), justAfter);
    take.add(noteOff(60, 200.01), TrackId::generate(), justAfter);
    const auto notes = take.notes(200.1, justAfter);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].startBeats == doctest::Approx(16.0 - 0.06));
}

TEST_CASE("Take: the first beat of a pass played a hair early is written on it, whole")
{
    // Heard half a millisecond before the loop goes round: the next pass's
    // downbeat, not a note at the end of the pattern cut to nothing.
    TakeBuilder take{patternTiming()};
    const auto track = TrackId::generate();
    const Position nearEnd{300.0, 8.0 - 0.0005, true};
    take.add(noteOn(60, 100, 300.0), track, nearEnd);
    take.add(noteOff(60, 300.25), track, nearEnd);
    const auto notes = take.notes(301.0, nearEnd);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].startBeats == doctest::Approx(0.0));
    CHECK(notes[0].lengthBeats == doctest::Approx(0.5).epsilon(0.01));
}

TEST_CASE("Take: a note held over the end of the loop ends with the pattern")
{
    TakeBuilder take{patternTiming()};
    const auto track = TrackId::generate();
    take.add(noteOn(60, 100, 100.0 + 7.75), track, playing); // beat 15.5
    take.add(noteOff(60, 100.0 + 8.75), track, playing);     // two beats later, past the wrap
    take.stop(110.0, playing);
    const auto notes = take.notes(110.0, playing);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].startBeats == doctest::Approx(15.5));
    CHECK(notes[0].lengthBeats == doctest::Approx(0.5));
}

TEST_CASE("Take: the pedal lengthens a note to its release")
{
    TakeBuilder take{patternTiming()};
    const auto track = TrackId::generate();
    take.add(pedal(true, 100.0), track, playing);
    take.add(noteOn(60, 100, 100.5), track, playing);
    take.add(noteOff(60, 100.75), track, playing); // the key up after half a beat...
    take.add(pedal(false, 101.5), track, playing); // ...the pedal up two beats after the press
    const auto notes = take.notes(102.0, playing);
    REQUIRE(notes.size() == 1);
    CHECK_FALSE(notes[0].open);
    CHECK(notes[0].lengthBeats == doctest::Approx(2.0));
}

TEST_CASE("Take: a second pass adds to the first, and a note played again is not doubled")
{
    PatternRig rig;
    auto& harness = rig.harness;
    TakeBuilder take{patternTiming()};
    // First pass: beat 0. Second pass (8 s later): beat 0 again, and beat 2.
    take.add(noteOn(60, 100, 100.0), harness.trackId, playing);
    take.add(noteOff(60, 100.25), harness.trackId, playing);
    take.add(noteOn(60, 100, 108.0), harness.trackId, playing);
    take.add(noteOff(60, 108.25), harness.trackId, playing);
    take.add(noteOn(67, 100, 109.0), harness.trackId, playing);
    take.add(noteOff(67, 109.25), harness.trackId, playing);
    take.stop(110.0, playing);

    Ids ids;
    const auto destination = TakeDestination{PlayMode::pattern, rig.pattern, {}, 0.0, 0.0};
    REQUIRE(executeTake(harness,
                        takeCommands(harness.state, destination, take.notes(110.0, playing), ids.take())));
    const auto& notes = rig.clip().notes;
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].pitch == 60);
    CHECK(notes[0].startBeats == doctest::Approx(0.0));
    CHECK(notes[1].pitch == 67);
    CHECK(notes[1].startBeats == doctest::Approx(2.0));
}

TEST_CASE("Take: a track without a row in the pattern gets one, in the same group")
{
    PatternRig rig;
    auto& harness = rig.harness;
    const auto other = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(other, "Basse")).ok());
    const auto before = json::write(harness.state.toValue());

    TakeBuilder take{patternTiming()};
    take.add(noteOn(36, 110, 100.0), other, playing);
    take.add(noteOff(36, 100.5), other, playing);
    take.stop(101.0, playing);

    Ids ids;
    const auto commands = takeCommands(harness.state,
                                       TakeDestination{PlayMode::pattern, rig.pattern, {}, 0.0, 0.0},
                                       take.notes(101.0, playing),
                                       ids.take());
    REQUIRE(commands.size() == 2);
    CHECK(commands[0]->type() == AddPatternTrack::commandType);
    REQUIRE(executeTake(harness,
                        takeCommands(harness.state,
                                     TakeDestination{PlayMode::pattern, rig.pattern, {}, 0.0, 0.0},
                                     take.notes(101.0, playing),
                                     ids.take())));
    const auto* row = harness.state.findPattern(rig.pattern)->findClipForTrack(other);
    REQUIRE(row != nullptr);
    CHECK(row->id == ids.rows[0]);
    REQUIRE(row->notes.size() == 1);
    CHECK(row->notes[0].pitch == 36);

    REQUIRE(harness.bus.undo().ok());
    CHECK(json::write(harness.state.toValue()) == before);
}

TEST_CASE("Take: in song mode the take is a pattern of its own, laid where it began")
{
    Harness harness;
    const auto before = json::write(harness.state.toValue());
    TakeTiming timing;
    timing.beatsAt = beatsAt;
    TakeBuilder take{timing};
    // The song at bar 3 (beat 8, 4 s) when the clock read 50 s.
    const Position song{50.0, 4.0, true};
    take.add(noteOn(72, 100, 50.5), harness.trackId, song); // beat 9
    take.add(noteOff(72, 51.0), harness.trackId, song);
    take.stop(52.0, song);

    Ids ids;
    TakeDestination destination;
    destination.mode = PlayMode::song;
    destination.pattern = PatternId::generate();
    destination.placement = PlacementId::generate();
    destination.atBeats = 8.0;
    destination.lengthBeats = 4.0;
    REQUIRE(
        executeTake(harness, takeCommands(harness.state, destination, take.notes(52.0, song), ids.take())));

    const auto* pattern = harness.state.findPattern(destination.pattern);
    REQUIRE(pattern != nullptr);
    CHECK(pattern->lengthBeats == doctest::Approx(4.0));
    const auto* row = pattern->findClipForTrack(harness.trackId);
    REQUIRE(row != nullptr);
    REQUIRE(row->notes.size() == 1);
    CHECK(row->notes[0].startBeats == doctest::Approx(1.0)); // beat 9 of the song, beat 1 of its pattern
    const auto* placement = harness.state.findPlacement(destination.placement);
    REQUIRE(placement != nullptr);
    CHECK(placement->patternId == destination.pattern);
    CHECK(placement->startBeats == doctest::Approx(8.0));

    REQUIRE(harness.bus.undo().ok());
    CHECK(json::write(harness.state.toValue()) == before);
}
