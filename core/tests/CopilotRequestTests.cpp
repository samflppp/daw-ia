#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/copilot/Tools.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/model/CopilotRequest.h"
#include "daw/ui/model/LaneEditing.h"

#include <cmath>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;
namespace request = daw::ui::copilot;

namespace
{

// "ouvre un omnisphere et cree des accords triste dans un pattern en 140 bpm",
// as the copilot stages it once the $new: names are minted.
struct Omnisphere
{
    Omnisphere()
    {
        const auto tempo = state.tempoPoints().front().id.toString();
        const auto ref = Value::object({{"format", Value{"VST3"}},
                                        {"identifier", Value{"spectrasonics-omnisphere"}},
                                        {"name", Value{"Omnisphere"}}});
        steps = {
            {"tempo.set_bpm", Value::object({{"pointId", Value{tempo}}, {"beatsPerMinute", Value{140.0}}})},
            {"track.add",
             Value::object({{"trackId", Value{track.toString()}},
                            {"name", Value{"Omnisphere"}},
                            {"volumeDb", Value{0.0}}})},
            {"plugin.insert",
             Value::object({{"trackId", Value{track.toString()}},
                            {"index", Value{0}},
                            {"plugin",
                             Value::object({{"id", Value{PluginId::generate().toString()}},
                                            {"ref", ref},
                                            {"bypassed", Value{false}}})}})},
            {"clip.create_midi",
             Value::object({{"trackId", Value{track.toString()}},
                            {"clipId", Value{clip.toString()}},
                            {"startBeats", Value{0.0}},
                            {"lengthBeats", Value{16.0}}})},
            {std::string{copilot::generationToolName},
             Value::object({{"clipId", Value{clip.toString()}},
                            {"role", Value{"chords"}},
                            {"density", Value{"sparse"}},
                            {"key", Value::object({{"tonic", Value{"A"}}, {"mode", Value{"minor"}}})}})},
        };
    }

    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    TrackId track{TrackId::generate()};
    ClipId clip{ClipId::generate()};
    std::vector<CommandQueue::Step> steps;
};

} // namespace

TEST_CASE("the omnisphere request is tried on a copy, then applied as one copilot entry")
{
    Omnisphere fixture;
    const auto before = json::write(fixture.state.toValue());
    const auto model = StyleModel::fallback();

    const auto expansion = request::expand(fixture.state, fixture.registry, fixture.steps, model);
    REQUIRE(expansion.ok());
    CHECK(json::write(fixture.state.toValue()) == before); // the copy moved, not the project

    // Only commands of the registry reach the bus: the generation is notes.
    for (const auto& step : expansion.steps)
        CHECK(step.type != copilot::generationToolName);

    CommandBus bus{fixture.state, fixture.registry};
    std::vector<std::unique_ptr<Command>> commands;
    for (const auto& step : expansion.steps)
    {
        auto created = fixture.registry.create(step.type, step.payload);
        REQUIRE(created.ok());
        commands.push_back(std::move(created).value());
    }
    GroupOptions group{};
    group.label = "ouvre un omnisphere et cree des accords triste dans un pattern en 140 bpm";
    group.origin.actor = Actor::copilot;
    REQUIRE(bus.executeGroup(std::move(commands), group).ok());
    CHECK(bus.undoDepth() == 1);

    // What was asked for is there.
    CHECK(fixture.state.tempoPoints().front().beatsPerMinute == doctest::Approx(140.0));
    const auto* track = fixture.state.findTrack(fixture.track);
    REQUIRE(track != nullptr);
    REQUIRE(track->plugins.size() == 1);
    CHECK(track->plugins.front().ref.name == "Omnisphere");

    // Chords: every onset a triad of A minor, in its key.
    const auto* row = fixture.state.findClip(fixture.clip);
    REQUIRE(row != nullptr);
    REQUIRE_FALSE(row->notes.empty());
    const Key aMinor{9, Mode::minor};
    std::map<long long, std::vector<int>> onsets;
    for (const auto& note : row->notes)
    {
        CHECK(inScale(note.pitch, aMinor));
        onsets[std::llround(note.startBeats * 960.0)].push_back(note.pitch);
    }
    for (const auto& [at, pitches] : onsets)
    {
        CAPTURE(at);
        REQUIRE(pitches.size() == 3);
        std::vector<WeightedPitch> heard;
        for (const auto pitch : pitches)
            heard.push_back({pitch, 1.0});
        const auto chord = detectChord(heard, aMinor);
        REQUIRE(chord.has_value());
        for (const auto pitch : pitches)
            CHECK(isChordTone(pitch, *chord, aMinor));
    }

    // One Ctrl+Z takes all of it back.
    REQUIRE(bus.undo().ok());
    CHECK(json::write(fixture.state.toValue()) == before);
}

TEST_CASE("the copy names the first call it refuses, and nothing moves")
{
    Omnisphere fixture;
    const auto before = json::write(fixture.state.toValue());

    // The generation aimed at a row nobody created.
    fixture.steps[4].payload =
        Value::object({{"clipId", Value{ClipId::generate().toString()}}, {"role", Value{"chords"}}});
    auto expansion = request::expand(fixture.state, fixture.registry, fixture.steps, StyleModel::fallback());
    REQUIRE_FALSE(expansion.ok());
    CHECK(expansion.refusal->index == 4);
    CHECK(expansion.refusal->code == ErrorCode::notFound);

    // A command the registry refuses, before it.
    fixture.steps[1].payload = Value::object({{"name", Value{"Omnisphere"}}});
    expansion = request::expand(fixture.state, fixture.registry, fixture.steps, StyleModel::fallback());
    REQUIRE_FALSE(expansion.ok());
    CHECK(expansion.refusal->index == 1);

    // A constraint outside the contract.
    Omnisphere other;
    other.steps[4].payload =
        Value::object({{"clipId", Value{other.clip.toString()}}, {"role", Value{"sad"}}});
    expansion = request::expand(other.state, other.registry, other.steps, StyleModel::fallback());
    REQUIRE_FALSE(expansion.ok());
    CHECK(expansion.refusal->index == 4);

    CHECK(json::write(fixture.state.toValue()) == before);
}

TEST_CASE("the generation tool is offered to the model beside the commands")
{
    const auto tool = copilot::generationTool();
    CHECK(tool.name == "pattern.generate");
    CHECK(tool.offeredToModel);
    const auto* required = tool.schema.find("required");
    REQUIRE(required != nullptr);
    CHECK(required->size() == 1);
    CHECK(tool.schema.find("properties")->find("form") != nullptr);
}

namespace
{

// A track with two audio clips on its own line, as a person leaves it.
struct TwoLoops
{
    TwoLoops()
    {
        CommandBus bus{state, registry};
        REQUIRE(bus.execute(std::make_unique<AddTrack>(track, "Boucle")).ok());
        SampleRef sample{};
        sample.blob.digest = std::string(BlobRef::digestLength, 'b');
        sample.blob.byteCount = 88200;
        sample.name = "Loop 01.wav";
        sample.format = "wav";
        sample.seconds = 1.0;
        REQUIRE(bus.execute(std::make_unique<PlaceAudio>(first, track, sample, 0.0)).ok());
        REQUIRE(bus.execute(std::make_unique<PlaceAudio>(second, track, sample, 4.0)).ok());
    }

    // The request as the copilot sends it, applied as one copilot entry.
    void applyAsCopilot(ProjectState& target, const std::vector<CommandQueue::Step>& steps) const
    {
        const auto expansion = request::expand(target, registry, steps, StyleModel::fallback());
        REQUIRE(expansion.ok());
        CommandBus bus{target, registry};
        std::vector<std::unique_ptr<Command>> commands;
        for (const auto& step : expansion.steps)
        {
            auto created = registry.create(step.type, step.payload);
            REQUIRE(created.ok());
            commands.push_back(std::move(created).value());
        }
        GroupOptions group{};
        group.label = "retire la boucle";
        group.origin.actor = Actor::copilot;
        const auto outcome = bus.executeGroup(std::move(commands), group);
        REQUIRE_MESSAGE(outcome.ok(), (outcome.ok() ? std::string{} : outcome.error().message));
    }

    [[nodiscard]] static CommandQueue::Step removeAudio(AudioClipId clipId)
    {
        return {"audio.remove", Value::object({{"clipId", Value{clipId.toString()}}})};
    }

    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    TrackId track{TrackId::generate()};
    AudioClipId first{AudioClipId::generate()};
    AudioClipId second{AudioClipId::generate()};
};

} // namespace

TEST_CASE("the copilot's audio.remove leaves the project the screen's Suppr leaves")
{
    TwoLoops fixture;
    const auto own = ProjectState::laneOfTrack(fixture.track);
    REQUIRE(fixture.state.findLane(own) != nullptr);

    SUBCASE("one clip of two: the line stays, for both")
    {
        auto byCopilot = fixture.state;
        fixture.applyAsCopilot(byCopilot, {TwoLoops::removeAudio(fixture.first)});
        CHECK(byCopilot.findLane(own) != nullptr);
    }

    SUBCASE("the last one: the same project, line gone, by either path")
    {
        auto byCopilot = fixture.state;
        fixture.applyAsCopilot(byCopilot,
                               {TwoLoops::removeAudio(fixture.first), TwoLoops::removeAudio(fixture.second)});

        auto byScreen = fixture.state;
        CommandBus screenBus{byScreen, fixture.registry};
        REQUIRE(daw::ui::laneEditing::removeBlocks(screenBus, byScreen, {fixture.first, fixture.second}, {}));

        CHECK(byCopilot.findLane(own) == nullptr);
        CHECK(json::write(byCopilot.toValue()) == json::write(byScreen.toValue()));
    }

    SUBCASE("track.remove takes its empty line too")
    {
        auto byCopilot = fixture.state;
        fixture.applyAsCopilot(
            byCopilot, {{"track.remove", Value::object({{"trackId", Value{fixture.track.toString()}}})}});
        CHECK(byCopilot.findLane(own) == nullptr);
    }
}
