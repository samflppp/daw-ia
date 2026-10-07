#include "TestSupport.h"
#include "daw/domain/buses/Shared.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/domain/serialization/Json.h"

#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

// The smart buses (S24): what is proposed, what is not, and the commands
// that make a proposal, in one group, undone at once.

namespace
{

PluginInstance equaliser(double highPass)
{
    PluginInstance plugin{};
    plugin.id = PluginId::generate();
    plugin.ref =
        PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::equaliser}, "Égaliseur"};
    plugin.params = {{std::string{internal::highPassFrequency}, highPass}};
    return plugin;
}

PluginInstance compressor(double threshold)
{
    PluginInstance plugin{};
    plugin.id = PluginId::generate();
    plugin.ref =
        PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::compressor}, "Compresseur"};
    plugin.params = {{std::string{internal::threshold}, threshold}};
    return plugin;
}

PluginInstance reverb(double mix, char digest)
{
    PluginInstance plugin{};
    plugin.id = PluginId::generate();
    plugin.ref = PluginRef{std::string{PluginRef::vst3Format}, "abcdefabcdefabcdefabcdefabcdefab", "Room"};
    plugin.params = {{"mix", mix}};
    plugin.state.digest = std::string(BlobRef::digestLength, digest);
    plugin.state.byteCount = 128;
    return plugin;
}

const buses::CategoryOf category = [](const PluginRef& ref)
{ return ref.name == "Room" ? "reverb" : "other"; };

struct Session
{
    TrackId track(const std::string& name, std::vector<PluginInstance> plugins)
    {
        const auto id = TrackId::generate();
        REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(id, name)).ok());
        for (std::size_t index = 0; index < plugins.size(); ++index)
            REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(id, plugins[index], index)).ok());
        return id;
    }

    Harness harness;
};

} // namespace

TEST_CASE("Six tracks with the same equaliser: one group, the odd ones left out")
{
    Session session;
    std::vector<TrackId> same;
    for (int index = 0; index < 6; ++index)
        same.push_back(session.track("Voix " + std::to_string(index + 1), {equaliser(120.0)}));
    const auto other = session.track("Autre réglage", {equaliser(200.0)});
    const auto sends = session.track("Avec envoi", {equaliser(120.0)});
    const auto bus = TrackId::generate();
    REQUIRE(session.harness.bus.execute(std::make_unique<AddBus>(bus, "Réverb")).ok());
    REQUIRE(session.harness.bus.execute(std::make_unique<SetTrackSend>(sends, bus, -10.0)).ok());
    const auto followed = session.track("Suivi", {equaliser(120.0), compressor(-20.0)});

    const auto proposals = buses::propose(session.harness.state, category);
    REQUIRE(proposals.size() == 1);
    const auto& group = proposals.front();
    CHECK(group.way == buses::Way::group);
    CHECK(group.tracks == std::vector<TrackId>{same.begin(), same.end()});
    MESSAGE(group.sentence);
    CHECK(group.sentence.find("même son") != std::string::npos);
    static_cast<void>(other);
    static_cast<void>(followed);
}

TEST_CASE("The same compressor: a group, and the sentence says the sound changes")
{
    Session session;
    session.track("Kick", {compressor(-18.0)});
    session.track("Snare", {compressor(-18.0)});
    const auto proposals = buses::propose(session.harness.state, category);
    REQUIRE(proposals.size() == 1);
    CHECK(proposals.front().sentence.find("compression de groupe, le son change") != std::string::npos);
}

TEST_CASE("The same reverb by a send, a close one said, one followed by another effect never")
{
    Session session;
    session.track("Voix", {reverb(0.3, 'a')});
    session.track("Choeur", {reverb(0.3, 'a')});
    session.track("Proche", {reverb(0.31, 'b')});
    session.track("Suivie", {reverb(0.3, 'a'), equaliser(120.0)});
    const auto proposals = buses::propose(session.harness.state, category);
    REQUIRE(proposals.size() == 1);
    CHECK(proposals.front().way == buses::Way::send);
    CHECK(proposals.front().tracks.size() == 3);
    CHECK(proposals.front().certainty == buses::Certainty::close);
    MESSAGE(proposals.front().sentence);
    CHECK(proposals.front().sentence.find("état interne différent") != std::string::npos);

    // Without a catalogue that says « reverb », nothing.
    CHECK(
        buses::propose(session.harness.state, [](const PluginRef&) { return std::string{"other"}; }).empty());
}

TEST_CASE(
    "A proposal kept: the bus, its one effect, the tracks sent or routed to it — one Ctrl+Z, to the byte")
{
    for (const bool group : {true, false})
    {
        Session session;
        const auto a = session.track("A", {group ? equaliser(150.0) : reverb(0.4, 'c')});
        const auto b = session.track("B", {group ? equaliser(150.0) : reverb(0.4, 'c')});
        const auto proposals = buses::propose(session.harness.state, category);
        REQUIRE(proposals.size() == 1);
        const auto before = json::write(session.harness.state.toValue());
        const auto depth = session.harness.bus.undoDepth();

        const auto bus = TrackId::generate();
        const auto plugin = PluginId::generate();
        GroupOptions options{};
        options.label = "bus intelligent";
        REQUIRE(
            session.harness.bus.executeGroup(buses::compile(proposals.front(), bus, "Bus", plugin), options)
                .ok());
        CHECK(session.harness.bus.undoDepth() == depth + 1);

        const auto* made = session.harness.state.findStrip(bus);
        REQUIRE(made != nullptr);
        REQUIRE(made->plugins.size() == 1);
        CHECK(made->plugins.front().id == plugin);
        CHECK(made->plugins.front().params == proposals.front().plugin.params);
        for (const auto id : {a, b})
        {
            const auto* track = session.harness.state.findTrack(id);
            REQUIRE(track != nullptr);
            CHECK(track->plugins.empty());
            if (group)
                CHECK(track->output == bus);
            else
                CHECK((track->findSend(bus) != nullptr && track->findSend(bus)->levelDb == 0.0));
        }

        REQUIRE(session.harness.bus.undo().ok());
        CHECK(json::write(session.harness.state.toValue()) == before);
    }
}
