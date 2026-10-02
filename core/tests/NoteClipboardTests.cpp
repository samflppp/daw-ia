#include "TestSupport.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/model/NoteClipboard.h"

#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::ui;

namespace
{

// Two patterns of one bar, two channels, a few notes in pattern 1.
struct Rack
{
    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    CommandBus bus{state, registry};
    TrackId kick{TrackId::generate()};
    TrackId hat{TrackId::generate()};
    PatternId first{PatternId::generate()};
    PatternId second{PatternId::generate()};
    ClipId kickRow{ClipId::generate()};
    std::vector<NoteId> kicks;

    Rack()
    {
        REQUIRE(bus.execute(std::make_unique<AddTrack>(kick, "Kick", 0.0)).ok());
        REQUIRE(bus.execute(std::make_unique<AddTrack>(hat, "Hat", 0.0)).ok());
        REQUIRE(bus.execute(std::make_unique<CreatePattern>(first, "", 4.0)).ok());
        REQUIRE(bus.execute(std::make_unique<CreatePattern>(second, "", 4.0)).ok());
        REQUIRE(bus.execute(std::make_unique<AddPatternTrack>(first, kickRow, kick)).ok());
        for (const auto beat : {1.0, 2.0, 3.0})
        {
            Note note{};
            note.id = NoteId::generate();
            note.pitch = 36;
            note.startBeats = beat;
            note.lengthBeats = 0.25;
            REQUIRE(bus.execute(std::make_unique<AddNote>(kickRow, note)).ok());
            kicks.push_back(note.id);
        }
    }

    // Runs a plan the way a panel does: one group.
    void run(PastePlan plan)
    {
        GroupOptions group{};
        group.label = "coller";
        REQUIRE(bus.executeGroup(std::move(plan.commands), group).ok());
    }
};

} // namespace

TEST_CASE("Copied notes are values: they paste into another pattern, one entry, one undo")
{
    Rack rack;
    const auto copied =
        copyNotes(*rack.state.findPattern(rack.first), rack.kick, {rack.kicks[0], rack.kicks[1]});
    REQUIRE(copied.rows.size() == 1);
    CHECK(copied.rows[0].notes[0].startBeats == 0.0); // relative to the earliest
    CHECK(copied.spanBeats == doctest::Approx(1.25));

    // The notes copied from are removed: the clipboard does not care.
    REQUIRE(rack.bus.execute(std::make_unique<RemoveNote>(rack.kickRow, rack.kicks[0])).ok());

    const auto depth = rack.bus.undoDepth();
    auto plan = planPaste(rack.state, rack.second, copied, {rack.kick}, 0.0, false);
    CHECK(plan.pasted.size() == 2);
    rack.run(std::move(plan));

    const auto* row = rack.state.findPattern(rack.second)->findClipForTrack(rack.kick);
    REQUIRE(row != nullptr); // the row was opened in the same group
    CHECK(row->notes.size() == 2);
    CHECK(rack.bus.undoDepth() == depth + 1);

    REQUIRE(rack.bus.undo().ok());
    CHECK(rack.state.findPattern(rack.second)->findClipForTrack(rack.kick) == nullptr);
}

TEST_CASE("Notes picked in two blocks keep the gap the ear heard, and go each to its own track")
{
    // A kick late in a block at beat 16, a hat early in the next one at 20:
    // a beat and a half apart in the song, though 3.5 and 0.0 in their
    // patterns.
    Rack rack;
    Note late{};
    late.id = NoteId::generate();
    late.pitch = 36;
    late.startBeats = 3.5;
    late.lengthBeats = 0.25;
    Note early = late;
    early.id = NoteId::generate();
    early.pitch = 42;
    early.startBeats = 0.0;

    const auto copied = copyFromBlocks({{20.0, rack.hat, early}, {16.0, rack.kick, late}});
    REQUIRE(copied.rows.size() == 2);
    CHECK(copied.originBeats == 3.5); // where the earliest note sits in its pattern

    const auto& hats = copied.rows[0];
    const auto& kicks = copied.rows[1];
    CHECK(hats.track == rack.hat);
    CHECK(kicks.track == rack.kick);
    CHECK(kicks.notes.front().startBeats == 0.0);
    CHECK(hats.notes.front().startBeats == doctest::Approx(0.5));

    // Pasted at beat 1 of the second pattern: each row on its own track,
    // the gap kept, a row opened for the hat.
    auto plan = planPaste(rack.state, rack.second, copied, {}, 1.0, false);
    CHECK(plan.pasted.size() == 2);
    rack.run(std::move(plan));
    const auto* pattern = rack.state.findPattern(rack.second);
    REQUIRE(pattern != nullptr);
    const auto* kickRow = pattern->findClipForTrack(rack.kick);
    const auto* hatRow = pattern->findClipForTrack(rack.hat);
    REQUIRE(kickRow != nullptr);
    REQUIRE(hatRow != nullptr);
    CHECK(kickRow->notes.front().startBeats == 1.0);
    CHECK(hatRow->notes.front().startBeats == doctest::Approx(1.5));
    CHECK(hatRow->notes.front().pitch == 42);
}

TEST_CASE("Pasting on notes already there doubles nothing")
{
    Rack rack;
    const auto copied = copyNotes(*rack.state.findPattern(rack.first), rack.kick, rack.kicks);
    auto plan = planPaste(rack.state, rack.first, copied, {}, copied.originBeats, false);
    CHECK(plan.pasted.empty());
    CHECK(plan.skipped == 3);
    CHECK(plan.commands.empty());
}

TEST_CASE("A row goes to the channel selected at paste time, or to its own")
{
    Rack rack;
    const auto copied = copyNotes(*rack.state.findPattern(rack.first), rack.kick, rack.kicks);
    rack.run(planPaste(rack.state, rack.first, copied, {rack.hat}, copied.originBeats, false));

    const auto* hatRow = rack.state.findPattern(rack.first)->findClipForTrack(rack.hat);
    REQUIRE(hatRow != nullptr);
    CHECK(hatRow->notes.size() == 3);
}

TEST_CASE("Ctrl+V leaves out what falls past the pattern, Ctrl+B lengthens it to the bar")
{
    Rack rack;
    const auto copied = copyNotes(*rack.state.findPattern(rack.first), rack.kick, rack.kicks);

    // At the playhead, beat 2: the copies land at 2, 3, 4 — the last one is
    // past the end of a one-bar pattern.
    auto pasted = planPaste(rack.state, rack.second, copied, {rack.kick}, 2.0, false);
    CHECK(pasted.pasted.size() == 2);
    CHECK(pasted.skipped == 1);

    // Ctrl+B: right after the copied span (1 to 3.25, so 2.25 rounded up to a
    // bar), and the pattern grows to hold it.
    const auto at = duplicateAt(copied, 1.0, 4.0);
    CHECK(at == 5.0);
    const auto before = json::write(rack.state.toValue());
    rack.run(planPaste(rack.state, rack.first, copied, {rack.kick}, at, true));
    CHECK(rack.state.findPattern(rack.first)->lengthBeats == 8.0);
    CHECK(rack.state.findPattern(rack.first)->findClipForTrack(rack.kick)->notes.size() == 6);

    REQUIRE(rack.bus.undo().ok());
    CHECK(json::write(rack.state.toValue()) == before);
}
