#include "TestSupport.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/ui/model/CanvasBands.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::ui::CanvasBand;
using daw::ui::CanvasBands;
using daw::ui::CanvasExtension;
namespace canvas = daw::ui::canvas;

namespace
{

// A pattern "Beat" with three rows: a kick on a sampler channel, an 808 and a
// lead, laid twice on its own line; a second pattern, lead only, laid on the
// same line.
struct Song
{
    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    CommandBus bus{state, registry};
    TrackId kick{TrackId::generate()};
    TrackId bass{TrackId::generate()};
    TrackId lead{TrackId::generate()};
    PatternId beat{PatternId::generate()};
    PatternId hook{PatternId::generate()};
    ClipId kickRow{ClipId::generate()};
    ClipId bassRow{ClipId::generate()};
    ClipId leadRow{ClipId::generate()};
    ClipId hookRow{ClipId::generate()};

    Song()
    {
        for (const auto& [id, name] :
             {std::pair{kick, "Kick"}, std::pair{bass, "808"}, std::pair{lead, "Lead"}})
            REQUIRE(bus.execute(std::make_unique<AddTrack>(id, name, 0.0)).ok());

        SampleRef sample{};
        sample.blob.digest = std::string(BlobRef::digestLength, 'c');
        sample.blob.byteCount = 44100;
        sample.name = "Kick.wav";
        sample.format = "wav";
        sample.seconds = 0.5;
        REQUIRE(bus.execute(std::make_unique<SetTrackSample>(kick, sample)).ok());

        REQUIRE(bus.execute(std::make_unique<CreatePattern>(beat, "Beat", 4.0)).ok());
        REQUIRE(bus.execute(std::make_unique<AddPatternTrack>(beat, kickRow, kick)).ok());
        REQUIRE(bus.execute(std::make_unique<AddPatternTrack>(beat, bassRow, bass)).ok());
        REQUIRE(bus.execute(std::make_unique<AddPatternTrack>(beat, leadRow, lead)).ok());
        REQUIRE(bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), beat, 0.0)).ok());
        REQUIRE(bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), beat, 4.0)).ok());

        REQUIRE(bus.execute(std::make_unique<CreatePattern>(hook, "Hook", 4.0, false)).ok());
        REQUIRE(bus.execute(std::make_unique<AddPatternTrack>(hook, hookRow, lead)).ok());
        REQUIRE(bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), hook, 8.0, line())).ok());
    }

    NoteId add(ClipId row, int pitch, double start, double length = 0.25)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = pitch;
        note.startBeats = start;
        note.lengthBeats = length;
        REQUIRE(bus.execute(std::make_unique<AddNote>(row, note)).ok());
        return note.id;
    }

    [[nodiscard]] LaneId line() const { return ProjectState::laneOfPattern(beat); }
};

} // namespace

TEST_CASE("a line unfolds into one band per track, in rack order, framed on what it plays")
{
    Song song;
    for (const auto start : {0.0, 1.0, 2.0, 3.0})
        song.add(song.kickRow, 36, start);
    song.add(song.bassRow, 34, 0.0, 2.0);
    song.add(song.bassRow, 41, 2.0, 2.0);
    song.add(song.leadRow, 72, 0.0);
    song.add(song.hookRow, 79, 1.0);

    CanvasBands bands;
    static_cast<void>(bands.refresh(song.state));
    const auto& line = bands.of(song.line());
    REQUIRE(line.size() == 3);

    // The kick, a sampler on one pitch: one row, no room.
    CHECK(line[0] == CanvasBand{song.kick, 36, 36});
    // The 808: its notes, and two semitones either side.
    CHECK(line[1] == CanvasBand{song.bass, 32, 43});
    // The lead: both patterns laid on the line count.
    CHECK(line[2] == CanvasBand{song.lead, 70, 81});
    CHECK(canvas::rowCount(line) == 1 + 12 + 12);
}

TEST_CASE("a row with no note yet is an octave from the channel pitch, one row for a sampler")
{
    Song song;
    CanvasBands bands;
    static_cast<void>(bands.refresh(song.state));
    const auto& line = bands.of(song.line());
    REQUIRE(line.size() == 3);
    CHECK(line[0].rows() == 1);
    CHECK(line[1] == CanvasBand{song.bass, 60, 71});

    // A line with no pattern block has no band.
    CHECK(bands.of(LaneId::generate()).empty());
}

TEST_CASE("the rows are sorted once, and again only when their notes change")
{
    Song song;
    song.add(song.leadRow, 72, 3.0);
    song.add(song.leadRow, 74, 1.0);

    CanvasBands bands;
    CHECK(bands.refresh(song.state) == 4); // kick, 808, lead, hook
    const auto* row = bands.notes(song.beat, song.lead);
    REQUIRE(row != nullptr);
    REQUIRE(row->notes.size() == 2);
    CHECK(row->notes[0].startBeats == doctest::Approx(1.0));

    // Nothing changed: nothing sorted.
    CHECK(bands.refresh(song.state) == 0);

    // A block moved: nothing sorted either, the notes are the same.
    const auto placement = song.state.arrangement().front().id;
    REQUIRE(song.bus.execute(std::make_unique<MovePlacement>(placement, 16.0)).ok());
    CHECK(bands.refresh(song.state) == 0);

    // A note of the 808 changed: that row only.
    song.add(song.bassRow, 40, 0.0);
    CHECK(bands.refresh(song.state) == 1);
    CHECK(bands.builds() == 5);
}

TEST_CASE("a pattern alone has a band for every channel of the rack, rows or not (S19)")
{
    Song song;
    song.add(song.leadRow, 72, 0.0);
    song.add(song.leadRow, 76, 1.0);
    CanvasBands bands;
    static_cast<void>(bands.refresh(song.state));

    // Hook has a lead row only; the kick and the 808 get a band all the same,
    // framed as a row with no note.
    const auto hook = bands.ofPattern(song.state, song.hook);
    REQUIRE(hook.size() == 3);
    CHECK(hook[0].track == song.kick);
    CHECK(hook[1].track == song.bass);
    CHECK(hook[2].track == song.lead);
    CHECK(hook[0].rows() == 1);  // a sampler: its one pitch
    CHECK(hook[1].rows() == 12); // an instrument: an octave
    CHECK(hook[2].rows() == 12); // the hook's lead row is empty too

    // Beat's lead is framed on its notes, two semitones either side.
    const auto beat = bands.ofPattern(song.state, song.beat);
    REQUIRE(beat.size() == 3);
    CHECK(beat[2].low == 70);
    CHECK(beat[2].high == 78);
}

TEST_CASE("a row is read again when a velocity changes: the canvas draws it (S19)")
{
    Song song;
    song.add(song.leadRow, 72, 0.0);
    CanvasBands bands;
    static_cast<void>(bands.refresh(song.state));
    const auto note = bands.notes(song.beat, song.lead)->notes.front();

    REQUIRE(song.bus.execute(std::make_unique<SetNoteVelocity>(song.leadRow, note.id, 30)).ok());
    CHECK(bands.refresh(song.state) == 1);
    CHECK(bands.notes(song.beat, song.lead)->notes.front().velocity == 30);
}

TEST_CASE("only the notes that can cross the window are visited")
{
    Song song;
    song.add(song.leadRow, 72, 0.0, 3.0); // long: reaches into [2, 3)
    for (int step = 1; step < 16; ++step)
        song.add(song.leadRow, 74, step * 0.25);

    CanvasBands bands;
    static_cast<void>(bands.refresh(song.state));
    const auto* row = bands.notes(song.beat, song.lead);
    REQUIRE(row != nullptr);

    const auto [first, last] = row->within(2.0, 3.0);
    // The long one first, then the four that start in the window.
    CHECK(row->notes[first].startBeats == doctest::Approx(0.0));
    CHECK(row->notes[last - 1].startBeats == doctest::Approx(2.75));
    CHECK(last - first < row->notes.size());
}

TEST_CASE("the zoom crosses from blocks to notes on both axes at once")
{
    const canvas::Scale scale{};

    // Dezoomed: a row is thinner than two pixels, nothing to grab.
    CHECK(canvas::rowHeight(6.0, scale) < 1.5);
    CHECK_FALSE(canvas::grabbable(6.0, canvas::rowHeight(6.0, scale), scale));
    CHECK(canvas::approach(canvas::rowHeight(6.0, scale), scale) == doctest::Approx(0.0));

    // Half way: the grid shows, the notes are not yet grabbed.
    const auto middle = canvas::rowHeight(24.0, scale);
    CHECK(canvas::approach(middle, scale) > 0.0);
    CHECK(canvas::approach(middle, scale) < 1.0);
    CHECK_FALSE(canvas::grabbable(24.0, middle, scale));

    // A sixteenth of eight pixels is a row of seven: grabbable.
    CHECK(canvas::rowHeight(32.0, scale) == doctest::Approx(7.0));
    CHECK(canvas::grabbable(32.0, canvas::rowHeight(32.0, scale), scale));

    // Never taller than a key of the piano roll.
    CHECK(canvas::rowHeight(192.0, scale) == doctest::Approx(14.0));

    // A line stretched to its minimum height does not make its notes
    // grabbable at a zoom where a sixteenth is too thin to aim at.
    CHECK_FALSE(canvas::grabbable(12.0, canvas::fittedRow(1, canvas::rowHeight(12.0, scale), 16, 34), scale));
}

TEST_CASE("a line is as tall as its rows, never less than a playlist line")
{
    CHECK(canvas::lineHeight(0, 7.0, 16, 34) == 34);
    CHECK(canvas::lineHeight(3, 1.0, 16, 34) == 34);
    CHECK(canvas::lineHeight(25, 7.0, 16, 34) == 16 + 175);
    CHECK(canvas::fittedRow(3, 1.0, 16, 34) == doctest::Approx(6.0));
    CHECK(canvas::fittedRow(25, 7.0, 16, 191) == doctest::Approx(7.0));
}

TEST_CASE("rows added by hand widen a band, within MIDI")
{
    const CanvasBand band{TrackId::generate(), 60, 64};
    CHECK(canvas::extended(band, CanvasExtension{3, 2}) == CanvasBand{band.track, 58, 67});
    CHECK(canvas::extended(band, CanvasExtension{200, 200}) == CanvasBand{band.track, 0, 127});
}
