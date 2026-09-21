#include "EngineTestSupport.h"
#include "daw/domain/commands/TempoCommands.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <memory>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

namespace
{

// How long the Edit actually renders for, in seconds.
//
// This is the whole point of the test. Reading edit.tempoSequence back, or
// reading ProjectState, would only prove that a number was stored where it was
// put. A tempo is a speed: the thing it changes is how long eight beats last,
// so that is what gets measured.
double renderedSeconds(tracktion::Edit& edit)
{
    const auto rendered = tracktion::test_utilities::renderToAudioBuffer(edit);
    if (rendered.sampleRate <= 0.0)
        return 0.0;

    return static_cast<double>(rendered.buffer.getNumSamples()) / rendered.sampleRate;
}

// Eight beats of music, so the arithmetic of a tempo change is readable: at
// 120 BPM they last four seconds.
void fillEightBeats(EngineHarness& harness)
{
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 8.0)).ok());

    for (int beat = 0; beat < 8; ++beat)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = 60;
        note.velocity = 100;
        note.startBeats = static_cast<double>(beat);
        note.lengthBeats = 1.0;
        REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clipId, note)).ok());
    }
}

} // namespace

TEST_CASE("The tempo of the origin changes how long the music lasts")
{
    EngineHarness harness;
    fillEightBeats(harness);

    const auto atDefault = renderedSeconds(harness.host.edit());
    MESSAGE("8 beats at 120 BPM = " << atDefault << " s");
    CHECK(atDefault == doctest::Approx(4.0).epsilon(0.02));

    REQUIRE(harness.bus.execute(std::make_unique<SetTempoPointBpm>(ProjectState::originTempoPointId(), 240.0))
                .ok());

    const auto twiceAsFast = renderedSeconds(harness.host.edit());
    MESSAGE("8 beats at 240 BPM = " << twiceAsFast << " s");
    CHECK(twiceAsFast == doctest::Approx(atDefault / 2.0).epsilon(0.02));
}

TEST_CASE("A tempo change halfway through is heard halfway through")
{
    EngineHarness harness;
    fillEightBeats(harness);

    const auto atDefault = renderedSeconds(harness.host.edit());

    // Four beats at 120, then four at 240: 2 s + 1 s instead of 4 s.
    const auto pointId = TempoPointId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(pointId, 4.0, 240.0)).ok());

    const auto withChange = renderedSeconds(harness.host.edit());
    MESSAGE("4 beats at 120 then 4 at 240 = " << withChange << " s");

    // Three quarters of the original, and not a half: the point only rules the
    // beats after it. A sequence projected as a single tempo would give 2 s.
    CHECK(withChange == doctest::Approx(atDefault * 0.75).epsilon(0.02));

    // Undo is projected like anything else, with no engine code of its own.
    REQUIRE(harness.bus.undo().ok());
    CHECK(renderedSeconds(harness.host.edit()) == doctest::Approx(atDefault).epsilon(0.02));

    REQUIRE(harness.bus.redo().ok());
    CHECK(renderedSeconds(harness.host.edit()) == doctest::Approx(withChange).epsilon(0.02));
}

TEST_CASE("Moving a tempo point moves where the music speeds up")
{
    EngineHarness harness;
    fillEightBeats(harness);

    const auto pointId = TempoPointId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<InsertTempoPoint>(pointId, 4.0, 240.0)).ok());
    const auto atFour = renderedSeconds(harness.host.edit());

    // The same two tempos, the change one bar later: 3 s + 0.5 s.
    REQUIRE(harness.bus.execute(std::make_unique<MoveTempoPoint>(pointId, 6.0)).ok());
    const auto atSix = renderedSeconds(harness.host.edit());
    MESSAGE("change at beat 4 = " << atFour << " s, at beat 6 = " << atSix << " s");

    CHECK(atSix > atFour);
    CHECK(atSix == doctest::Approx(3.5).epsilon(0.02));

    // Removing it gives the whole passage back to the origin tempo.
    REQUIRE(harness.bus.execute(std::make_unique<RemoveTempoPoint>(pointId)).ok());
    CHECK(renderedSeconds(harness.host.edit()) == doctest::Approx(4.0).epsilon(0.02));
}
