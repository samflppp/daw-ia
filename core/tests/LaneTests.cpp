#include "TestSupport.h"
#include "daw/domain/commands/LaneCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/serialization/Json.h"

#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

SampleRef kick()
{
    SampleRef sample{};
    sample.blob.digest = std::string(BlobRef::digestLength, 'a');
    sample.blob.byteCount = 44100;
    sample.name = "Kick 01.wav";
    sample.format = "wav";
    sample.seconds = 0.5;
    return sample;
}

// An envelope as the S16 build wrote it: version 3, and payloads that name no
// line, because lines did not exist.
Value s16Envelope(std::string type, Value payload)
{
    return Value::object(
        {{"v", Value{std::int64_t{3}}},
         {"id", Value{CommandId::generate().toString()}},
         {"type", Value{std::move(type)}},
         {"at", Value{std::int64_t{1758182400123456}}},
         {"gesture", Value{}},
         {"group", Value{}},
         {"origin", Value::object({{"actor", Value{std::string{"user"}}}, {"context", Value{}}})},
         {"payload", std::move(payload)}});
}

std::vector<LaneId> laneIds(const ProjectState& state)
{
    std::vector<LaneId> ids;
    for (const auto& lane : state.lanes())
        ids.push_back(lane.id);
    return ids;
}

} // namespace

TEST_CASE(
    "an S16 journal replays into one line per pattern, then one per audio track, and serialises as before")
{
    Harness harness;
    const auto first = PatternId::generate();
    const auto second = PatternId::generate();

    // Audio first on purpose: the S16 playlist drew pattern lines above audio
    // lines whatever the order they were made in.
    const auto sample = kick();
    for (const auto& envelope : std::vector<Value>{
             s16Envelope("audio.place",
                         Value::object({{"clipId", Value{AudioClipId::generate().toString()}},
                                        {"trackId", Value{harness.trackId.toString()}},
                                        {"sample", sample.toValue()},
                                        {"startBeats", Value{0.0}}})),
             s16Envelope("pattern.create",
                         Value::object({{"patternId", Value{first.toString()}},
                                        {"name", Value{std::string{"Beat"}}},
                                        {"lengthBeats", Value{4.0}}})),
             s16Envelope("pattern.create",
                         Value::object({{"patternId", Value{second.toString()}},
                                        {"name", Value{std::string{}}},
                                        {"lengthBeats", Value{8.0}}})),
             s16Envelope("pattern.place",
                         Value::object({{"placementId", Value{PlacementId::generate().toString()}},
                                        {"patternId", Value{second.toString()}},
                                        {"startBeats", Value{4.0}}}))})
    {
        REQUIRE(harness.bus.executeSerialized(envelope).ok());
    }

    CHECK(laneIds(harness.state) == std::vector<LaneId>{ProjectState::laneOfPattern(first),
                                                        ProjectState::laneOfPattern(second),
                                                        ProjectState::laneOfTrack(harness.trackId)});
    CHECK(harness.state.arrangement().front().laneId == ProjectState::laneOfPattern(second));
    CHECK(harness.state.audioClips().front().laneId == ProjectState::laneOfTrack(harness.trackId));

    // Nothing in the serialised state says "line": an old project hashes and
    // compares the way it did before S17.
    const auto text = json::write(harness.state.toValue());
    CHECK(text.find("lane") == std::string::npos);

    auto reread = ProjectState::fromValue(harness.state.toValue());
    REQUIRE(reread.ok());
    CHECK(reread.value() == harness.state);
}

TEST_CASE("a block dragged to another line is one entry, and the undo puts it back on its line and beat")
{
    Harness harness;
    const auto patternId = PatternId::generate();
    const auto placementId = PlacementId::generate();
    const auto bass = LaneId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(placementId, patternId, 0.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(bass, "Basse", 99)).ok());
    CHECK(harness.state.laneIndex(bass).value() == 1);

    const auto before = harness.state.toValue();

    // One gesture: sideways, then down.
    const auto gesture = harness.bus.beginGesture("glisser");
    REQUIRE(
        harness.bus.execute(std::make_unique<MovePlacement>(placementId, 4.0), ExecuteOptions{gesture}).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<MovePlacement>(placementId, 8.0, bass), ExecuteOptions{gesture})
            .ok());

    const auto* moved = harness.state.findPlacement(placementId);
    REQUIRE(moved != nullptr);
    CHECK(moved->laneId == bass);
    CHECK(moved->startBeats == doctest::Approx(8.0));

    // A line the pattern was not born on is written down.
    CHECK(json::write(harness.state.toValue()).find(bass.toString()) != std::string::npos);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);
    CHECK(harness.state.findPlacement(placementId)->laneId == ProjectState::laneOfPattern(patternId));

    // A line that does not exist is refused, and nothing moves.
    CHECK_FALSE(
        harness.bus.execute(std::make_unique<MovePlacement>(placementId, 2.0, LaneId::generate())).ok());
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("removing a line takes its blocks, and the undo gives back the same playlist")
{
    Harness harness;
    const auto patternId = PatternId::generate();
    const auto drums = LaneId::generate();
    const auto keep = LaneId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0, false)).ok());
    CHECK(harness.state.lanes().empty());

    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(drums, "Drums", 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(keep, "", 1)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 0.0, drums))
            .ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 4.0, keep))
                .ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 8.0, drums))
            .ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<PlaceAudio>(
                    AudioClipId::generate(), harness.trackId, kick(), 2.0, drums))
                .ok());

    const auto before = harness.state.toValue();

    REQUIRE(harness.bus.execute(std::make_unique<RemoveLane>(drums)).ok());
    CHECK(harness.state.lanes().size() == 1);
    CHECK(harness.state.arrangement().size() == 1);
    CHECK(harness.state.audioClips().empty());

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);

    auto reread = ProjectState::fromValue(before);
    REQUIRE(reread.ok());
    CHECK(reread.value() == harness.state);
}

TEST_CASE("reordering and renaming a line moves no block")
{
    Harness harness;
    const auto a = LaneId::generate();
    const auto b = LaneId::generate();
    const auto c = LaneId::generate();
    const auto patternId = PatternId::generate();
    const auto placementId = PlacementId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "", 4.0, false)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(a, "A", 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(b, "B", 1)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(c, "C", 2)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(placementId, patternId, 0.0, b)).ok());

    const auto before = harness.state.toValue();

    const auto gesture = harness.bus.beginGesture("glisser");
    REQUIRE(harness.bus.execute(std::make_unique<MoveLane>(b, 0), ExecuteOptions{gesture}).ok());
    REQUIRE(harness.bus.execute(std::make_unique<MoveLane>(b, 2), ExecuteOptions{gesture}).ok());
    CHECK(laneIds(harness.state) == std::vector<LaneId>{a, c, b});
    CHECK(harness.state.findPlacement(placementId)->laneId == b);

    REQUIRE(harness.bus.execute(std::make_unique<RenameLane>(b, "Basse")).ok());
    CHECK(harness.state.findLane(b)->name == "Basse");

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findLane(b)->name == "B");
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("pattern.remove takes the pattern's own line when it is left empty and unnamed, and only then")
{
    Harness harness;
    const auto patternId = PatternId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(patternId, "Beat", 4.0)).ok());
    REQUIRE(
        harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), patternId, 0.0)).ok());

    const auto own = ProjectState::laneOfPattern(patternId);
    const auto before = harness.state.toValue();

    SUBCASE("empty and unnamed: it goes, and the undo brings it back")
    {
        REQUIRE(harness.bus.execute(std::make_unique<RemovePattern>(patternId)).ok());
        CHECK(harness.state.lanes().empty());
        REQUIRE(harness.bus.undo().ok());
        CHECK(harness.state.toValue() == before);
    }

    SUBCASE("named by the user: it stays")
    {
        REQUIRE(harness.bus.execute(std::make_unique<RenameLane>(own, "Drums")).ok());
        REQUIRE(harness.bus.execute(std::make_unique<RemovePattern>(patternId)).ok());
        CHECK(harness.state.findLane(own) != nullptr);
    }

    SUBCASE("holding another block: it stays")
    {
        const auto other = PatternId::generate();
        REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(other, "Other", 4.0, false)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), other, 4.0, own))
                    .ok());
        REQUIRE(harness.bus.execute(std::make_unique<RemovePattern>(patternId)).ok());
        CHECK(harness.state.findLane(own) != nullptr);
    }
}

TEST_CASE("an audio clip changes line like a placement does")
{
    Harness harness;
    const auto clipId = AudioClipId::generate();
    const auto loops = LaneId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<PlaceAudio>(clipId, harness.trackId, kick(), 0.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<CreateLane>(loops, "Loops", 0)).ok());
    const auto before = harness.state.toValue();

    REQUIRE(harness.bus.execute(std::make_unique<MoveAudio>(clipId, 2.0, loops)).ok());
    CHECK(harness.state.findAudioClip(clipId)->laneId == loops);
    // Its track, and so its sound, did not change.
    CHECK(harness.state.findAudioClip(clipId)->trackId == harness.trackId);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("the new payloads survive the round-trip through the registry")
{
    const auto registry = CommandRegistry::withBuiltinCommands();
    const auto lane = LaneId::generate();

    const std::vector<std::shared_ptr<Command>> commands{
        std::make_shared<CreateLane>(lane, "Basse", 3),
        std::make_shared<RemoveLane>(lane),
        std::make_shared<RenameLane>(lane, "Lead"),
        std::make_shared<MoveLane>(lane, 1),
        std::make_shared<MovePlacement>(PlacementId::generate(), 4.0, lane),
        std::make_shared<PlacePattern>(PlacementId::generate(), PatternId::generate(), 4.0, lane),
        std::make_shared<CreatePattern>(PatternId::generate(), "", 4.0, false),
        std::make_shared<MoveAudio>(AudioClipId::generate(), 1.0, lane),
    };

    for (const auto& command : commands)
    {
        auto rebuilt = registry.create(command->type(), command->payload());
        REQUIRE(rebuilt.ok());
        CHECK(rebuilt.value()->payload() == command->payload());
    }

    // Equal payloads prove nothing if both lose the same field: what a rebuilt
    // command does is read on the state it builds.
    ProjectState state;
    auto rebuilt = registry.create("lane.create", CreateLane{lane, "Basse", 3}.payload());
    REQUIRE(rebuilt.ok());
    REQUIRE(rebuilt.value()->apply(state).ok());
    REQUIRE(state.findLane(lane) != nullptr);
    CHECK(state.findLane(lane)->name == "Basse");
}
