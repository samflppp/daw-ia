#include "TestSupport.h"
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

SampleRef kick(double seconds = 0.5)
{
    SampleRef sample{};
    sample.blob.digest = std::string(BlobRef::digestLength, 'a');
    sample.blob.byteCount = 44100;
    sample.name = "Kick 01.wav";
    sample.format = "wav";
    sample.seconds = seconds;
    return sample;
}

} // namespace

TEST_CASE("a track becomes a sampler channel, and undoing gives it back to its chain")
{
    Harness harness;

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, kick())).ok());
    REQUIRE(harness.state.findTrack(harness.trackId)->sample.has_value());
    CHECK(harness.state.findTrack(harness.trackId)->sample->name == "Kick 01.wav");

    REQUIRE(harness.bus.undo().ok());
    CHECK_FALSE(harness.state.findTrack(harness.trackId)->sample.has_value());
}

TEST_CASE("a sample with no bytes, no name or no length is refused")
{
    Harness harness;

    auto noBytes = kick();
    noBytes.blob = {};
    CHECK(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, noBytes)).code() ==
          ErrorCode::invalidArgument);

    auto noLength = kick(0.0);
    CHECK(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, noLength)).code() ==
          ErrorCode::invalidArgument);

    auto badFormat = kick();
    badFormat.format = ".WAV";
    CHECK(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, badFormat)).code() ==
          ErrorCode::invalidArgument);
}

TEST_CASE("an audio clip is laid, moved in one gesture, removed, and comes back at its rank")
{
    Harness harness;
    const auto first = AudioClipId::generate();
    const auto second = AudioClipId::generate();

    REQUIRE(harness.bus.execute(std::make_unique<PlaceAudio>(first, harness.trackId, kick(), 0.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlaceAudio>(second, harness.trackId, kick(), 8.0)).ok());

    const auto gesture = harness.bus.beginGesture("déplacer un clip audio");
    ExecuteOptions options{};
    options.gesture = gesture;
    const auto depth = harness.bus.undoDepth();
    for (double beat = 1.0; beat <= 4.0; beat += 1.0)
        REQUIRE(harness.bus.execute(std::make_unique<MoveAudio>(first, beat), options).ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == depth + 1);
    CHECK(harness.state.findAudioClip(first)->startBeats == doctest::Approx(4.0));

    const auto before = harness.state.toValue();
    REQUIRE(harness.bus.execute(std::make_unique<RemoveAudio>(first)).ok());
    CHECK(harness.state.findAudioClip(first) == nullptr);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("removing a track takes its audio clips, and undoing lays them back")
{
    Harness harness;
    const auto clipId = AudioClipId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<PlaceAudio>(clipId, harness.trackId, kick(), 4.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, kick())).ok());

    const auto before = harness.state.toValue();
    REQUIRE(harness.bus.execute(std::make_unique<RemoveTrack>(harness.trackId)).ok());
    CHECK(harness.state.audioClips().empty());

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("a project without samples serialises exactly as before samples existed")
{
    Harness harness;
    const auto text = json::write(harness.state.toValue());
    CHECK(text.find("\"audio\"") == std::string::npos);
    CHECK(text.find("\"sample\"") == std::string::npos);
}

TEST_CASE("samples and audio clips survive a round-trip and a replay from their payloads")
{
    Harness harness;
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSample>(harness.trackId, kick())).ok());
    REQUIRE(
        harness.bus
            .execute(std::make_unique<PlaceAudio>(AudioClipId::generate(), harness.trackId, kick(2.0), 16.0))
            .ok());

    const auto restored = ProjectState::fromValue(harness.state.toValue());
    REQUIRE(restored.ok());
    CHECK(restored.value() == harness.state);

    const std::vector<std::shared_ptr<Command>> commands{
        std::make_shared<SetTrackSample>(harness.trackId, kick()),
        std::make_shared<SetTrackSample>(harness.trackId, std::nullopt),
        std::make_shared<PlaceAudio>(AudioClipId::generate(), harness.trackId, kick(), 2.0),
        std::make_shared<MoveAudio>(AudioClipId::generate(), 3.0),
        std::make_shared<RemoveAudio>(AudioClipId::generate())};

    for (const auto& command : commands)
    {
        auto rebuilt = harness.registry.create(command->type(), command->payload());
        REQUIRE(rebuilt.ok());
        CHECK(rebuilt.value()->payload() == command->payload());
    }
}
