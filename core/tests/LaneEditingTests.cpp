#include "TestSupport.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/ui/model/LaneEditing.h"

#include <memory>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
namespace laneEditing = daw::ui::laneEditing;

namespace
{

SampleRef loop()
{
    SampleRef sample{};
    sample.blob.digest = std::string(BlobRef::digestLength, 'b');
    sample.blob.byteCount = 88200;
    sample.name = "Loop 01.wav";
    sample.format = "wav";
    sample.seconds = 1.0;
    return sample;
}

} // namespace

TEST_CASE("the last audio clip of a track takes its line with it, in the same entry")
{
    Harness harness;
    const auto first = AudioClipId::generate();
    const auto second = AudioClipId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<PlaceAudio>(first, harness.trackId, loop(), 0.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlaceAudio>(second, harness.trackId, loop(), 4.0)).ok());

    const auto own = ProjectState::laneOfTrack(harness.trackId);
    REQUIRE(harness.state.findLane(own) != nullptr);

    SUBCASE("one clip of two: the line stays")
    {
        REQUIRE(laneEditing::removeBlocks(harness.bus, harness.state, {first}, {}));
        CHECK(harness.state.findLane(own) != nullptr);
    }

    SUBCASE("the last one: the line goes, and one Ctrl+Z gives back both")
    {
        REQUIRE(laneEditing::removeBlocks(harness.bus, harness.state, {first}, {}));
        const auto before = harness.state.toValue();
        const auto depth = harness.bus.undoDepth();

        REQUIRE(laneEditing::removeBlocks(harness.bus, harness.state, {second}, {}));
        CHECK(harness.state.findLane(own) == nullptr);
        CHECK(harness.bus.undoDepth() == depth + 1);

        REQUIRE(harness.bus.undo().ok());
        CHECK(harness.state.toValue() == before);
    }

    SUBCASE("both at once: the line goes")
    {
        REQUIRE(laneEditing::removeBlocks(harness.bus, harness.state, {first, second}, {}));
        CHECK(harness.state.findLane(own) == nullptr);
    }

    SUBCASE("named by the user: it stays")
    {
        REQUIRE(harness.bus.execute(std::make_unique<RenameLane>(own, "Loops")).ok());
        REQUIRE(laneEditing::removeBlocks(harness.bus, harness.state, {first, second}, {}));
        CHECK(harness.state.findLane(own) != nullptr);
    }

    SUBCASE("holding a pattern block too: it stays")
    {
        const auto patternId = PatternId::generate();
        REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0, false)).ok());
        REQUIRE(
            harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 8.0, own))
                .ok());
        REQUIRE(laneEditing::removeBlocks(harness.bus, harness.state, {first, second}, {}));
        CHECK(harness.state.findLane(own) != nullptr);
    }

    SUBCASE("a clip moved onto a line of its own does not take the track's line")
    {
        const auto loops = LaneId::generate();
        REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(loops, "", 0)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<MoveAudio>(second, 4.0, loops)).ok());
        REQUIRE(laneEditing::removeBlocks(harness.bus, harness.state, {second}, {}));
        CHECK(harness.state.findLane(loops) != nullptr);
        CHECK(harness.state.findLane(own) != nullptr);
    }
}

TEST_CASE("removing a track takes its emptied line, and the undo gives back the same playlist")
{
    Harness harness;
    REQUIRE(harness.bus
                .execute(std::make_unique<PlaceAudio>(AudioClipId::generate(), harness.trackId, loop(), 0.0))
                .ok());
    const auto own = ProjectState::laneOfTrack(harness.trackId);
    const auto before = harness.state.toValue();
    const auto depth = harness.bus.undoDepth();

    REQUIRE(laneEditing::removeTrack(harness.bus, harness.state, harness.trackId));
    CHECK(harness.state.findLane(own) == nullptr);
    CHECK(harness.state.lanes().empty());
    CHECK(harness.bus.undoDepth() == depth + 1);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);
}
