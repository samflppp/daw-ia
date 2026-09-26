#include "daw/domain/commands/TempoCommands.h"
#include "daw/ui/model/NoteClipboard.h"
#include "daw/ui/model/TempoEditing.h"

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::ui::tempoEditing;

TEST_CASE("the wheel moves the tempo by whole beats per minute, inside the domain's range")
{
    CHECK(stepTempo(120.0, 1) == 121.0);
    CHECK(stepTempo(120.0, -3) == 117.0);

    // A tempo typed with a decimal lands on the whole number first.
    CHECK(stepTempo(120.5, 1) == 121.0);
    CHECK(stepTempo(120.5, -1) == 120.0);
    CHECK(stepTempo(120.5, 2) == 122.0);

    CHECK(stepTempo(ProjectState::maxTempo, 1) == ProjectState::maxTempo);
    CHECK(stepTempo(ProjectState::minTempo, -1) == ProjectState::minTempo);
}

TEST_CASE("a typed tempo accepts a point or a comma, and nothing outside the range")
{
    CHECK(parseTempo("140") == 140.0);
    CHECK(parseTempo(" 97.5 ") == 97.5);
    CHECK(parseTempo("97,5") == 97.5);

    CHECK_FALSE(parseTempo("").has_value());
    CHECK_FALSE(parseTempo("vite").has_value());
    CHECK_FALSE(parseTempo("140 bpm").has_value());
    CHECK_FALSE(parseTempo("19").has_value());
    CHECK_FALSE(parseTempo("301").has_value());
}

TEST_CASE("a typed signature is two numbers the domain accepts")
{
    CHECK(parseSignature("6/8") == TimeSignature{6, 8});
    CHECK(parseSignature(" 3 / 4 ") == TimeSignature{3, 4});

    CHECK_FALSE(parseSignature("6").has_value());
    CHECK_FALSE(parseSignature("6/7").has_value());
    CHECK_FALSE(parseSignature("0/4").has_value());
    CHECK_FALSE(parseSignature("a/b").has_value());

    CHECK(stepSignature(TimeSignature{6, 8}, 1) == TimeSignature{7, 8});
    CHECK(stepSignature(TimeSignature{1, 4}, -1) == TimeSignature{1, 4});
    CHECK(stepSignature(TimeSignature{16, 4}, 1) == TimeSignature{16, 4});
}

TEST_CASE("the first automation point lands on the playhead's bar, never on the origin's")
{
    ProjectState state;
    CHECK_FALSE(isAutomated(state));

    CHECK(automationStart(state, 0.0) == 4.0);
    CHECK(automationStart(state, 9.5) == 8.0);

    REQUIRE(state.insertTempoPoint(TempoPoint{TempoPointId::generate(), 8.0, 140.0}).ok());
    CHECK(isAutomated(state));
    CHECK(automationStart(state, 9.5) == 12.0);

    // In 6/8 a bar is three beats.
    REQUIRE(state.setTimeSignature(TimeSignature{6, 8}).ok());
    CHECK(automationStart(state, 0.0) == 3.0);
    CHECK(projectTempo(state) == 120.0);
}

TEST_CASE("Ctrl+B rounds the copy to the project's bar")
{
    daw::ui::CopiedNotes copied;
    copied.spanBeats = 2.25;
    CHECK(daw::ui::duplicateAt(copied, 1.0, 4.0) == 5.0);
    CHECK(daw::ui::duplicateAt(copied, 1.0, 3.0) == 4.0);
}
