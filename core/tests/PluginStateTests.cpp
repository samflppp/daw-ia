#include "TestSupport.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/commands/AutomationCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/serialization/Json.h"

#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
using daw::testing::RecordingObserver;

namespace
{

// A digest looks like what BLAKE3 produces: 64 lowercase hex characters. The
// domain never computes one, it only refuses a malformed one.
std::string digestOf(char fill)
{
    return std::string(StateBlobRef::digestLength, fill);
}

PluginInstance makeInstance(PluginId id, std::string name = "Synthe")
{
    PluginInstance plugin{};
    plugin.id = id;
    plugin.ref.format = std::string{PluginRef::vst3Format};
    plugin.ref.identifier = "1234567890abcdef1234567890abcdef";
    plugin.ref.name = std::move(name);
    return plugin;
}

std::unique_ptr<Command> insert(TrackId trackId, const PluginInstance& plugin, std::size_t index = 0)
{
    return std::make_unique<InsertPlugin>(trackId, plugin, index);
}

} // namespace

TEST_CASE("A plugin instance enters a chain through a command, at a chosen index")
{
    Harness harness;
    const auto first = PluginId::generate();
    const auto second = PluginId::generate();

    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(first, "Reverb"), 0)).ok());
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(second, "EQ"), 0)).ok());

    const auto* track = harness.state.findTrack(harness.trackId);
    REQUIRE(track != nullptr);
    REQUIRE(track->plugins.size() == 2);
    CHECK(track->plugins[0].id == second); // inserted in front
    CHECK(track->plugins[1].id == first);
}

TEST_CASE("An index past the end appends instead of failing")
{
    Harness harness;
    const auto id = PluginId::generate();

    // A replayed payload must not fail because the chain is shorter than it was.
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 99)).ok());

    const auto location = harness.state.pluginLocation(id);
    REQUIRE(location.ok());
    CHECK(location.value().index == 0);
}

TEST_CASE("Two instances of the same plugin binary are two identities")
{
    Harness harness;
    const auto first = PluginId::generate();
    const auto second = PluginId::generate();

    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(first), 0)).ok());
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(second), 1)).ok());

    const auto* track = harness.state.findTrack(harness.trackId);
    REQUIRE(track != nullptr);
    CHECK(track->plugins.size() == 2);
    CHECK(track->plugins[0].ref == track->plugins[1].ref);
}

TEST_CASE("Inserting the same plugin identity twice is refused")
{
    Harness harness;
    const auto id = PluginId::generate();
    const auto plugin = makeInstance(id);

    REQUIRE(harness.bus.execute(insert(harness.trackId, plugin, 0)).ok());

    const auto again = harness.bus.execute(insert(harness.trackId, plugin, 0));
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().code == ErrorCode::conflict);
    CHECK(harness.bus.undoDepth() == 1); // the refused command left no entry
}

TEST_CASE("An unsupported format is refused before anything is inserted")
{
    Harness harness;
    auto plugin = makeInstance(PluginId::generate());
    plugin.ref.format = "AudioUnit";

    const auto executed = harness.bus.execute(insert(harness.trackId, plugin, 0));
    REQUIRE_FALSE(executed.ok());
    CHECK(executed.error().code == ErrorCode::invalidArgument);
    CHECK(harness.state.findPlugin(plugin.id) == nullptr);
}

TEST_CASE("Removing a plugin and undoing brings back its parameters and its state")
{
    Harness harness;
    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(id, "cutoff", 0.75)).ok());

    StateBlobRef blob{};
    blob.digest = digestOf('a');
    blob.byteCount = 4096;
    REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(id, blob)).ok());

    const auto before = harness.state.toValue();

    REQUIRE(harness.bus.execute(std::make_unique<RemovePlugin>(id)).ok());
    CHECK(harness.state.findPlugin(id) == nullptr);

    REQUIRE(harness.bus.undo().ok());

    // Not just the instance: the whole serialised state has to be identical,
    // parameters, captured blob and position in the chain included.
    CHECK(harness.state.toValue() == before);
}

TEST_CASE("A knob sweep is one history entry, and one undo cancels all of it")
{
    Harness harness;
    RecordingObserver observer;
    harness.bus.addObserver(observer);

    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 0)).ok());

    const auto gesture = harness.bus.beginGesture("bouton cutoff");
    for (int frame = 0; frame < 40; ++frame)
    {
        const auto value = 0.01 * static_cast<double>(frame);
        REQUIRE(
            harness.bus
                .execute(std::make_unique<SetPluginParameter>(id, "cutoff", value), ExecuteOptions{gesture})
                .ok());
    }
    REQUIRE(harness.bus.endGesture(gesture).ok());

    CHECK(harness.bus.undoDepth() == 2); // the insert, and the whole sweep
    CHECK(observer.coalesced.size() == 39);
    CHECK(harness.bus.journal().size() == 2);

    const auto* plugin = harness.state.findPlugin(id);
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->findParam("cutoff") != nullptr);
    CHECK(plugin->findParam("cutoff")->value == doctest::Approx(0.39));

    // Undoing the first touch gives the parameter back to the plugin's own
    // state instead of pinning it to a default value.
    REQUIRE(harness.bus.undo().ok());
    plugin = harness.state.findPlugin(id);
    REQUIRE(plugin != nullptr);
    CHECK(plugin->params.empty());
    CHECK(plugin->findParam("cutoff") == nullptr);
}

TEST_CASE("Two parameters moved inside one gesture stay two history entries")
{
    Harness harness;
    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 0)).ok());

    const auto gesture = harness.bus.beginGesture("deux boutons");
    REQUIRE(
        harness.bus.execute(std::make_unique<SetPluginParameter>(id, "cutoff", 0.2), ExecuteOptions{gesture})
            .ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<SetPluginParameter>(id, "resonance", 0.4), ExecuteOptions{gesture})
                .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    // Undoing one movement must not move the other, so they cannot merge.
    CHECK(harness.bus.undoDepth() == 3);

    REQUIRE(harness.bus.undo().ok());
    const auto* plugin = harness.state.findPlugin(id);
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->findParam("cutoff") != nullptr);
    CHECK(plugin->findParam("cutoff")->value == doctest::Approx(0.2));
    CHECK(plugin->findParam("resonance") == nullptr);
}

TEST_CASE("Undoing a state capture restores the previous digest")
{
    Harness harness;
    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 0)).ok());

    StateBlobRef first{};
    first.digest = digestOf('b');
    first.byteCount = 1024;
    REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(id, first)).ok());

    StateBlobRef second{};
    second.digest = digestOf('c');
    second.byteCount = 2048;
    REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(id, second)).ok());

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findPlugin(id)->state == first);

    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.findPlugin(id)->state.isEmpty());

    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.findPlugin(id)->state == first);
}

TEST_CASE("A malformed digest is refused")
{
    Harness harness;
    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 0)).ok());

    StateBlobRef truncated{};
    truncated.digest = "abcdef";
    truncated.byteCount = 16;

    const auto executed = harness.bus.execute(std::make_unique<CapturePluginState>(id, truncated));
    REQUIRE_FALSE(executed.ok());
    CHECK(executed.error().code == ErrorCode::invalidArgument);
    CHECK(harness.state.findPlugin(id)->state.isEmpty());

    StateBlobRef sizeless{};
    sizeless.digest = digestOf('d');
    sizeless.byteCount = 0;
    CHECK_FALSE(harness.bus.execute(std::make_unique<CapturePluginState>(id, sizeless)).ok());
}

TEST_CASE("A parameter outside the normalised range is refused")
{
    Harness harness;
    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 0)).ok());

    CHECK_FALSE(harness.bus.execute(std::make_unique<SetPluginParameter>(id, "cutoff", 1.5)).ok());
    CHECK_FALSE(harness.bus.execute(std::make_unique<SetPluginParameter>(id, "cutoff", -0.1)).ok());
    CHECK(harness.state.findPlugin(id)->params.empty());
}

TEST_CASE("Parameters keep a sorted order, whatever the order they were touched in")
{
    Harness harness;
    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id), 0)).ok());

    for (const auto* paramId : {"volume", "attack", "cutoff"})
        REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(id, paramId, 0.5)).ok());

    const auto* plugin = harness.state.findPlugin(id);
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->params.size() == 3);
    CHECK(plugin->params[0].paramId == "attack");
    CHECK(plugin->params[1].paramId == "cutoff");
    CHECK(plugin->params[2].paramId == "volume");
}

TEST_CASE("Replaying the journal rebuilds a plugin chain exactly")
{
    Harness harness;
    const auto id = PluginId::generate();

    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id, "Basse"), 0)).ok());

    const auto gesture = harness.bus.beginGesture("cutoff");
    for (int frame = 0; frame < 10; ++frame)
        REQUIRE(harness.bus
                    .execute(
                        std::make_unique<SetPluginParameter>(id, "cutoff", 0.05 * static_cast<double>(frame)),
                        ExecuteOptions{gesture})
                    .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    REQUIRE(harness.bus.execute(std::make_unique<SetPluginBypassed>(id, true)).ok());

    StateBlobRef blob{};
    blob.digest = digestOf('e');
    blob.byteCount = 64 * 1024;
    REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(id, blob)).ok());

    const auto journal = harness.bus.journal();
    const auto expected = harness.state.toValue();

    // A second project, same track, replaying nothing but the serialised
    // envelopes — the way a file load or an MCP session will do it.
    Harness replayed;
    ProjectState& target = replayed.state;
    REQUIRE(target.removeTrack(replayed.trackId).ok());
    Track track{};
    track.id = harness.trackId;
    track.name = "Piste 1";
    REQUIRE(target.addTrack(track).ok());

    for (const auto& envelope : journal)
        REQUIRE(replayed.bus.executeSerialized(envelope).ok());

    CHECK(target.toValue() == expected);
}

TEST_CASE("A plugin chain survives a serialisation round trip through text")
{
    Harness harness;
    const auto id = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(id, "Piano"), 0)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(id, "cutoff", 0.25)).ok());

    StateBlobRef blob{};
    blob.digest = digestOf('f');
    blob.byteCount = 12 * 1024 * 1024; // a sampler state, held by reference only
    REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(id, blob)).ok());

    const auto text = json::write(harness.state.toValue());

    // The heavy blob never enters the text: what is written is a digest.
    CHECK(text.find(blob.digest) != std::string::npos);
    CHECK(text.size() < 4096);

    const auto parsed = json::read(text);
    REQUIRE(parsed.ok());

    const auto restored = ProjectState::fromValue(parsed.value());
    REQUIRE(restored.ok());
    CHECK(restored.value() == harness.state);
}

// plugin.move (S24): the same instance, elsewhere in its chain.
TEST_CASE(
    "A plugin moved in its chain keeps its identity, settings, state and lines, and one undo puts it back")
{
    Harness harness;
    const auto a = PluginId::generate();
    const auto b = PluginId::generate();
    const auto c = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(a, "Réverb"), 0)).ok());
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(b, "EQ"), 1)).ok());
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(c, "Comp"), 2)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetPluginParameter>(a, "mix", 0.3)).ok());
    StateBlobRef blob{};
    blob.digest = digestOf('a');
    blob.byteCount = 1024;
    REQUIRE(harness.bus.execute(std::make_unique<CapturePluginState>(a, blob)).ok());
    REQUIRE(harness.bus
                .execute(std::make_unique<CreateAutomationLine>(AutomationLineId::generate(),
                                                                AutomationTarget::parameterOf(a, "mix")))
                .ok());

    const auto before = json::write(harness.state.toValue());
    const auto original = *harness.state.findPlugin(a);

    REQUIRE(harness.bus.execute(std::make_unique<MovePlugin>(a, 2)).ok());
    const auto* track = harness.state.findTrack(harness.trackId);
    REQUIRE(track->plugins.size() == 3);
    CHECK(track->plugins[0].id == b);
    CHECK(track->plugins[1].id == c);
    CHECK(track->plugins[2] == original);
    CHECK(harness.state.automationOfPlugin(a).size() == 1);

    REQUIRE(harness.bus.undo().ok());
    CHECK(json::write(harness.state.toValue()) == before);
}

TEST_CASE("A move past the end goes last, and an unknown plugin is refused")
{
    Harness harness;
    const auto a = PluginId::generate();
    const auto b = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(a), 0)).ok());
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(b), 1)).ok());

    REQUIRE(harness.bus.execute(std::make_unique<MovePlugin>(a, 99)).ok());
    CHECK(harness.state.findTrack(harness.trackId)->plugins.back().id == a);

    const auto refused = harness.bus.execute(std::make_unique<MovePlugin>(PluginId::generate(), 0));
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == ErrorCode::notFound);
}

TEST_CASE("A move replays from its envelope to the same chain")
{
    Harness harness;
    const auto a = PluginId::generate();
    const auto b = PluginId::generate();
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(a, "Un"), 0)).ok());
    REQUIRE(harness.bus.execute(insert(harness.trackId, makeInstance(b, "Deux"), 1)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<MovePlugin>(b, 0)).ok());

    const auto journal = harness.bus.journal();
    const auto expected = harness.state.toValue();

    Harness replayed;
    REQUIRE(replayed.state.removeTrack(replayed.trackId).ok());
    Track track{};
    track.id = harness.trackId;
    track.name = "Piste 1";
    REQUIRE(replayed.state.addTrack(track).ok());
    for (const auto& envelope : journal)
        REQUIRE(replayed.bus.executeSerialized(envelope).ok());
    CHECK(replayed.state.toValue() == expected);
}
