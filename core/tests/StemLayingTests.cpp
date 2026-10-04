#include "TestSupport.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/serialization/Json.h"
#include "daw/domain/stems/Laying.h"

#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;

namespace
{

SampleRef sample(char fill, const std::string& name)
{
    SampleRef out{};
    out.blob.digest = std::string(BlobRef::digestLength, fill);
    out.blob.byteCount = 1000;
    out.name = name;
    out.format = "wav";
    out.seconds = 180.0;
    return out;
}

stems::Laying fourStems(double startBeats)
{
    stems::Laying laying;
    laying.sourceName = "Chanson.wav";
    laying.startBeats = startBeats;
    const char fills[] = {'1', '2', '3', '4'};
    // Given out of order: the tracks are laid in the order of stems::names.
    for (const auto* name : {"other", "bass", "vocals", "drums"})
    {
        laying.stems.push_back(stems::Stem{name,
                                           sample(fills[laying.stems.size()], std::string{name} + ".wav"),
                                           TrackId::generate(),
                                           AudioClipId::generate()});
    }
    return laying;
}

struct Separated
{
    daw::testing::Harness harness;
    AudioClipId source{AudioClipId::generate()};

    Separated()
    {
        REQUIRE(harness.bus
                    .execute(std::make_unique<PlaceAudio>(
                        source, harness.trackId, sample('e', "Chanson.wav"), 8.0))
                    .ok());
    }

    void lay(const stems::Laying& laying)
    {
        auto commands = stems::commandsFor(harness.state, laying);
        REQUIRE(commands.ok());
        GroupOptions group{};
        group.label = stems::groupLabel(laying);
        REQUIRE(harness.bus.executeGroup(std::move(commands).value(), group).ok());
    }
};

} // namespace

TEST_CASE("Stems: four tracks, named and given their role, a clip each at the source's beat")
{
    Separated project;
    auto laying = fourStems(8.0);
    laying.replaced = project.source;
    project.lay(laying);

    const auto& state = project.harness.state;
    REQUIRE(state.tracks().size() == 5);
    const char* expected[] = {
        "Voix — Chanson.wav", "Batterie — Chanson.wav", "Basse — Chanson.wav", "Le reste — Chanson.wav"};
    const std::optional<MixRole> roles[] = {MixRole::vocal, MixRole::percussion, MixRole::bass, std::nullopt};
    for (std::size_t index = 0; index < 4; ++index)
    {
        const auto& track = state.tracks()[index + 1];
        CHECK(track.name == expected[index]);
        CHECK(track.role == roles[index]);
    }

    for (const auto& stem : laying.stems)
    {
        const auto* clip = state.findAudioClip(stem.clip);
        REQUIRE(clip != nullptr);
        CHECK(clip->trackId == stem.track);
        CHECK(clip->startBeats == doctest::Approx(8.0));
        CHECK(clip->sample == stem.sample);
    }
    CHECK(state.findAudioClip(project.source) == nullptr);
}

TEST_CASE("Stems: one Ctrl+Z takes the separation back, to the byte")
{
    Separated project;
    const auto before = json::write(project.harness.state.toValue());
    const auto depth = project.harness.bus.undoDepth();

    auto laying = fourStems(8.0);
    laying.replaced = project.source;
    project.lay(laying);
    CHECK(project.harness.bus.undoDepth() == depth + 1);

    REQUIRE(project.harness.bus.undo().ok());
    CHECK(json::write(project.harness.state.toValue()) == before);

    // And Ctrl+Y lays it again, the same.
    REQUIRE(project.harness.bus.redo().ok());
    CHECK(project.harness.state.tracks().size() == 5);
    CHECK(project.harness.state.findAudioClip(project.source) == nullptr);
}

TEST_CASE("Stems: a dropped file replaces nothing")
{
    Separated project;
    project.lay(fourStems(0.0));
    CHECK(project.harness.state.findAudioClip(project.source) != nullptr);
    CHECK(project.harness.state.tracks().size() == 5);
}

TEST_CASE("Stems: a bad laying is refused before any command")
{
    Separated project;

    auto unknown = fourStems(0.0);
    unknown.stems[0].name = "guitar";
    CHECK(stems::commandsFor(project.harness.state, unknown).code() == ErrorCode::invalidArgument);

    auto twice = fourStems(0.0);
    twice.stems[1].name = twice.stems[0].name;
    CHECK(stems::commandsFor(project.harness.state, twice).code() == ErrorCode::conflict);

    auto taken = fourStems(0.0);
    taken.stems[0].track = project.harness.trackId;
    CHECK(stems::commandsFor(project.harness.state, taken).code() == ErrorCode::conflict);

    auto gone = fourStems(0.0);
    gone.replaced = AudioClipId::generate();
    CHECK(stems::commandsFor(project.harness.state, gone).code() == ErrorCode::notFound);
}
