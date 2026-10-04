#include "TestSupport.h"
#include "daw/domain/commands/AutomationCommands.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/domain/copilot/StateView.h"
#include "daw/domain/copilot/Tools.h"
#include "daw/domain/serialization/Json.h"

#include <algorithm>

using namespace daw::domain;
using namespace daw::domain::copilot;
using daw::testing::Harness;

namespace
{

MachinePlugins machineWith(std::size_t count)
{
    MachinePlugins plugins{};
    for (std::size_t index = 0; index < count; ++index)
    {
        PluginRef ref{};
        ref.format = std::string{PluginRef::clapFormat};
        ref.identifier = "plugin." + std::to_string(index);
        ref.name = "Plugin " + std::to_string(index);
        plugins.available.push_back(std::move(ref));
    }

    return plugins;
}

} // namespace

TEST_CASE("every command of the registry is a tool, and every tool a command")
{
    const auto registry = CommandRegistry::withBuiltinCommands();
    const auto tools = toolsFor(registry);
    REQUIRE_MESSAGE(tools.ok(), tools.error().message);
    CHECK(tools.value().size() == registry.types().size());
}

TEST_CASE("a tool carries a schema the model can fill, and a sentence it can read")
{
    const auto tools = builtinTools();

    const auto found = std::find_if(
        tools.begin(), tools.end(), [](const Tool& tool) { return tool.name == "note.quantize"; });
    REQUIRE(found != tools.end());
    CHECK(!found->summary.empty());

    const auto* properties = found->schema.find("properties");
    REQUIRE(properties != nullptr);
    CHECK(properties->contains("clipId"));
    CHECK(properties->contains("noteIds"));
    CHECK(properties->contains("gridBeats"));

    const auto* required = found->schema.find("required");
    REQUIRE(required != nullptr);
    REQUIRE(required->asArray() != nullptr);
    CHECK(required->asArray()->size() == 3);
}

TEST_CASE("the list handed to the model leaves out what only the application can call")
{
    const auto tools = builtinTools();
    const auto offered = copilot::toValue(tools);
    REQUIRE(offered.asArray() != nullptr);

    // Three commands of the registry are not offered: capturing a plugin
    // state, making a sampler channel and laying an audio clip all need bytes,
    // and the copilot has none. Nor are the five automation gestures of the
    // screen: the copilot writes a line in one go, with automation.write.
    CHECK(offered.asArray()->size() == tools.size() - 8);

    const auto text = json::write(offered);
    CHECK(text.find("plugin.capture_state") == std::string::npos);
    CHECK(text.find("track.set_sample") == std::string::npos);
    CHECK(text.find("audio.place") == std::string::npos);
    CHECK(text.find("audio.move") != std::string::npos);
    CHECK(text.find("track.set_volume") != std::string::npos);
    CHECK(text.find("automation.write") != std::string::npos);
    CHECK(text.find("automation.move_point") == std::string::npos);
}

TEST_CASE("the summary describes pattern rows without carrying their notes")
{
    Harness harness;
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 4.0)).ok());

    for (int index = 0; index < 16; ++index)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = 48 + index;
        note.velocity = 100;
        note.startBeats = 0.25 * index;
        note.lengthBeats = 0.25;
        REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clipId, note)).ok());
    }

    const auto summary = summarise(harness.state, machineWith(2));
    const auto text = json::write(summary);

    // The track is there, and it no longer carries what it plays: the notes
    // live in a pattern now, and the summary says so in the same place.
    const auto* tracks = summary.find("tracks");
    REQUIRE(tracks != nullptr);
    REQUIRE(tracks->asArray() != nullptr);
    REQUIRE(tracks->asArray()->size() == 1);
    CHECK(tracks->asArray()->front().find("clips") == nullptr);

    const auto* patterns = summary.find("patterns");
    REQUIRE(patterns != nullptr);
    REQUIRE(patterns->asArray() != nullptr);
    REQUIRE(patterns->asArray()->size() == 1);

    const auto& pattern = patterns->asArray()->front();
    CHECK(pattern.doubleAt("lengthBeats").value() == doctest::Approx(4.0));

    // And where it is played, which is the placement and never the row.
    const auto* placements = pattern.find("placements");
    REQUIRE(placements != nullptr);
    REQUIRE(placements->asArray()->size() == 1);
    CHECK(placements->asArray()->front().doubleAt("startBeats").value() == doctest::Approx(0.0));

    const auto* clips = pattern.find("clips");
    REQUIRE(clips != nullptr);
    REQUIRE(clips->asArray()->size() == 1);

    const auto& clip = clips->asArray()->front();
    CHECK(clip.stringAt("trackId").value() == harness.trackId.toString());
    CHECK(clip.intAt("noteCount").value() == 16);
    CHECK(clip.intAt("lowestPitch").value() == 48);
    CHECK(clip.intAt("highestPitch").value() == 63);

    // And not one note identifier is in it.
    CHECK(text.find("\"notes\"") == std::string::npos);
    CHECK(text.find("velocity") == std::string::npos);

    // The tempo names the point "set the tempo to 140" has to aim at.
    const auto* tempo = summary.find("tempo");
    REQUIRE(tempo != nullptr);
    CHECK(tempo->stringAt("originPointId").value() == ProjectState::originTempoPointId().toString());

    // And the transport is there, because a copilot that cannot see it works
    // blind.
    const auto* transport = summary.find("transport");
    REQUIRE(transport != nullptr);
    CHECK(transport->boolAt("playing").value() == false);
    CHECK(transport->doubleAt("tempoAtPosition").value() == doctest::Approx(120.0));
}

TEST_CASE("the notes of a clip are asked for one clip at a time")
{
    Harness harness;
    const auto clipId = ClipId::generate();
    const auto noteId = NoteId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 4.0)).ok());
    REQUIRE(harness.bus.execute(Harness::addNote(clipId, noteId, 60)).ok());

    const auto notes = clipNotes(harness.state, clipId);
    REQUIRE(notes.ok());

    const auto* items = notes.value().find("notes");
    REQUIRE(items != nullptr);
    REQUIRE(items->asArray()->size() == 1);
    CHECK(items->asArray()->front().stringAt("id").value() == noteId.toString());

    // And a clip that is not there is an error, never an empty list: the
    // copilot must not quantize nothing and believe it worked.
    CHECK(clipNotes(harness.state, ClipId::generate()).error().code == ErrorCode::notFound);
}

TEST_CASE("a machine full of plugins is cut, said, and searchable")
{
    Harness harness;
    const auto plugins = machineWith(200);

    const auto summary = summarise(harness.state, plugins);
    const auto* machine = summary.find("machinePlugins");
    REQUIRE(machine != nullptr);

    // The whole number is told, and only part of the list is sent.
    CHECK(machine->intAt("total").value() == 200);
    REQUIRE(machine->find("listed") != nullptr);
    CHECK(machine->find("listed")->asArray()->size() == MachinePlugins::maxListed);

    // What the cut leaves out is still reachable.
    const auto found = findPlugins(plugins, "plugin 137");
    REQUIRE(found.find("found") != nullptr);
    REQUIRE(found.find("found")->asArray()->size() == 1);
    CHECK(found.find("found")->asArray()->front().stringAt("name").value() == "Plugin 137");
}

TEST_CASE("the summary says the rank a user reads, where the song ends, and the play mode")
{
    Harness harness;
    const auto first = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(first, 0.0, 4.0)).ok());

    const auto second = PatternId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreatePattern>(second, "Refrain", 8.0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<PlacePattern>(PlacementId::generate(), second, 12.0)).ok());

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, second)).ok());

    const auto summary = summarise(harness.state, machineWith(0));

    // "le pattern 1" is the one an unnamed pattern is shown as; a named one
    // keeps its name and still has its rank.
    const auto& patterns = *summary.find("patterns")->asArray();
    REQUIRE(patterns.size() == 2);
    CHECK(patterns[0].intAt("rank").value() == 1);
    CHECK(patterns[0].stringAt("label").value() == "Pattern 1");
    CHECK(patterns[1].intAt("rank").value() == 2);
    CHECK(patterns[1].stringAt("label").value() == "Refrain");

    // Beat 12 plus eight: "à la suite" starts at 20.
    CHECK(summary.doubleAt("arrangementEndBeats").value() == doctest::Approx(20.0));

    const auto* transport = summary.find("transport");
    CHECK(transport->stringAt("mode").value() == "pattern");
    CHECK(transport->stringAt("auditionedPatternId").value() == second.toString());
}

TEST_CASE("the summary carries the automation lines, and nothing when there are none")
{
    Harness harness;
    CHECK(summarise(harness.state, machineWith(0)).find("automation") == nullptr);

    const auto line = AutomationLineId::generate();
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(
                    line, AutomationTarget::volumeOf(ProjectState::masterTrackId())))
                .ok());

    const auto summary = summarise(harness.state, machineWith(0));
    const auto* lines = summary.find("automation");
    REQUIRE(lines != nullptr);
    REQUIRE(lines->asArray()->size() == 1);
    CHECK(lines->asArray()->front().stringAt("id").value() == line.toString());
}

TEST_CASE("the summary carries the direction, what it says together and its value, only when there is one")
{
    Harness harness;
    CHECK(summarise(harness.state, machineWith(0)).find("direction") == nullptr);

    direction::Direction wanted;
    for (const auto& [name, bpm] :
         {std::pair{std::string{"a.wav"}, 120.0}, std::pair{std::string{"b.wav"}, 92.0}})
    {
        direction::Reference reference;
        reference.reading.name = name;
        reference.reading.digest = std::string(64, name.front());
        reference.reading.bpm = bpm;
        reference.reading.key = generation::Key{9, generation::Mode::minor};
        wanted.references.push_back(reference);
    }
    harness.state.setDirection(wanted);

    const auto summary = summarise(harness.state, machineWith(0));
    const auto* direction = summary.find("direction");
    REQUIRE(direction != nullptr);
    const auto* says = direction->find("says");
    REQUIRE(says != nullptr);
    // The key they share, in words; the tempos they do not, said.
    CHECK(says->stringAt("key").value() == "La mineur");
    CHECK(says->find("bpm") == nullptr);
    REQUIRE(says->find("contradictions") != nullptr);
    CHECK(json::write(*says->find("contradictions")).find("120 BPM") != std::string::npos);
    // And the whole direction, as direction.set takes it back.
    REQUIRE(direction->find("value") != nullptr);
    CHECK(direction::Direction::fromValue(*direction->find("value")).value() == wanted);
}
