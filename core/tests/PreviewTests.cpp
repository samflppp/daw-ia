#include "TestSupport.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/ui/model/PatternPreviews.h"
#include "daw/ui/model/WaveformPeaks.h"

#include <cmath>
#include <memory>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::ui::PatternPreviews;
using daw::ui::WaveformPeaks;

namespace
{

// A pattern of four beats with one row, laid eight times.
struct LaidPattern
{
    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    CommandBus bus{state, registry};
    TrackId trackId{TrackId::generate()};
    PatternId patternId{PatternId::generate()};
    ClipId rowId{ClipId::generate()};
    std::vector<PlacementId> placements;

    LaidPattern()
    {
        Track track{};
        track.id = trackId;
        track.name = "Basse";
        REQUIRE(state.addTrack(track).ok());
        REQUIRE(bus.execute(std::make_unique<CreatePattern>(patternId, "", 4.0)).ok());
        REQUIRE(bus.execute(std::make_unique<AddPatternTrack>(patternId, rowId, trackId)).ok());
        for (int laying = 0; laying < 8; ++laying)
        {
            placements.push_back(PlacementId::generate());
            REQUIRE(
                bus.execute(std::make_unique<PlacePattern>(placements.back(), patternId, laying * 4.0)).ok());
        }
    }

    void addNote(int pitch, double start, double length = 1.0)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = pitch;
        note.startBeats = start;
        note.lengthBeats = length;
        REQUIRE(bus.execute(std::make_unique<AddNote>(rowId, note)).ok());
    }
};

} // namespace

TEST_CASE("A pattern laid eight times is previewed once")
{
    LaidPattern project;
    project.addNote(60, 0.0);

    PatternPreviews previews;
    CHECK(previews.refresh(project.state) == 1);
    CHECK(previews.builds() == 1);

    const auto* preview = previews.find(project.patternId);
    REQUIRE(preview != nullptr);
    CHECK(preview->notes.size() == 1);
}

TEST_CASE("Moving a placement rebuilds no preview, adding a note rebuilds one")
{
    LaidPattern project;
    project.addNote(60, 0.0);

    PatternPreviews previews;
    static_cast<void>(previews.refresh(project.state));

    REQUIRE(project.bus.execute(std::make_unique<MovePlacement>(project.placements[3], 40.0)).ok());
    CHECK(previews.refresh(project.state) == 0);
    CHECK(previews.rebuilt().empty());

    REQUIRE(project.bus.execute(std::make_unique<SetTrackVolume>(project.trackId, -6.0)).ok());
    CHECK(previews.refresh(project.state) == 0);

    // The one rebuilt is named: the playlist repaints its blocks and no others.
    project.addNote(67, 2.0);
    CHECK(previews.refresh(project.state) == 1);
    CHECK(previews.rebuilt() == std::vector<PatternId>{project.patternId});
    CHECK(previews.find(project.patternId)->notes.size() == 2);

    CHECK(previews.refresh(project.state) == 0);
    CHECK(previews.rebuilt().empty());

    // And an undo is a change of content like any other.
    REQUIRE(project.bus.undo().ok());
    CHECK(previews.refresh(project.state) == 1);
    CHECK(previews.find(project.patternId)->notes.size() == 1);

    CHECK(previews.builds() == 3);
}

TEST_CASE("A preview places notes by time across and by pitch down, highest on top")
{
    LaidPattern project;
    project.addNote(72, 0.0, 1.0); // top, first quarter
    project.addNote(60, 2.0, 2.0); // bottom, second half

    const auto preview = PatternPreviews::build(*project.state.findPattern(project.patternId));
    REQUIRE(preview.notes.size() == 2);

    const auto& high = preview.notes[0];
    const auto& low = preview.notes[1];
    CHECK(high.x == doctest::Approx(0.0));
    CHECK(high.width == doctest::Approx(0.25));
    CHECK(high.y == doctest::Approx(0.0));
    CHECK(low.x == doctest::Approx(0.5));
    CHECK(low.width == doctest::Approx(0.5));
    CHECK(low.y + low.height == doctest::Approx(1.0));
    CHECK(high.height == doctest::Approx(1.0 / 13.0));
}

TEST_CASE("A note past the pattern's end is not drawn, and one that overruns it is cut")
{
    LaidPattern project;
    project.addNote(60, 3.0, 4.0);
    project.addNote(60, 5.0, 1.0);

    const auto preview = PatternPreviews::build(*project.state.findPattern(project.patternId));
    REQUIRE(preview.notes.size() == 1);
    CHECK(preview.notes[0].x + preview.notes[0].width == doctest::Approx(1.0));
}

TEST_CASE("A removed pattern takes its preview with it")
{
    LaidPattern project;
    project.addNote(60, 0.0);
    PatternPreviews previews;
    static_cast<void>(previews.refresh(project.state));

    REQUIRE(project.bus.execute(std::make_unique<RemovePattern>(project.patternId)).ok());
    CHECK(previews.refresh(project.state) == 0);
    CHECK(previews.find(project.patternId) == nullptr);
}

TEST_CASE("Waveform peaks: measured once per bucket, gathered per column")
{
    constexpr double rate = 1000.0;
    std::vector<float> left(2000, 0.0f);
    std::vector<float> right(2000, 0.0f);

    // One second of silence, then a spike of +0.5 on the left and -0.8 on
    // the right a quarter of a second into the second second.
    left[1250] = 0.5f;
    right[1250] = -0.8f;
    const float* channels[] = {left.data(), right.data()};

    const auto peaks = WaveformPeaks::measure(channels, 2, 2000, rate);
    CHECK(peaks.seconds == doctest::Approx(2.0));
    CHECK(peaks.minimum.size() == 800);

    const auto whole = peaks.columns(0.0, 2.0, 2);
    REQUIRE(whole.size() == 2);
    CHECK(whole[0].minimum == 0.0f);
    CHECK(whole[0].maximum == 0.0f);
    CHECK(whole[1].maximum == doctest::Approx(0.5f));
    CHECK(whole[1].minimum == doctest::Approx(-0.8f));

    // A clip drawn past the end of its sample is silent there.
    const auto past = peaks.columns(1.5, 3.5, 4);
    CHECK(past[2].maximum == 0.0f);
    CHECK(past[3].minimum == 0.0f);
}
