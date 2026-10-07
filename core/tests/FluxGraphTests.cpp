#include "TestSupport.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/flux/Graph.h"
#include "daw/domain/project/InternalEffects.h"

#include <algorithm>
#include <memory>
#include <set>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

// The audio flux (S24), read from the project: the project of the exposition
// of 6 October 2026 — six tracks, a « Drums » bus the kick, the snare and the
// hats go out to, a « Réverb » bus the snare, the chords and the lead send to,
// an equaliser on the master.

namespace
{

PluginInstance effect(std::string_view identifier, std::string name)
{
    PluginInstance plugin{};
    plugin.id = PluginId::generate();
    plugin.ref.format = std::string{PluginRef::internalFormat};
    plugin.ref.identifier = std::string{identifier};
    plugin.ref.name = std::move(name);
    return plugin;
}

PluginInstance theirs(std::string name)
{
    PluginInstance plugin{};
    plugin.id = PluginId::generate();
    plugin.ref.format = std::string{PluginRef::vst3Format};
    plugin.ref.identifier = "0123456789abcdef0123456789abcdef";
    plugin.ref.name = std::move(name);
    return plugin;
}

struct Song
{
    Song()
    {
        REQUIRE(harness.state.removeTrack(harness.trackId).ok());
        for (auto* track : {&kick, &snare, &hats, &eightOhEight, &chords, &lead})
            *track = TrackId::generate();
        const std::pair<TrackId, const char*> names[] = {{kick, "Kick"},
                                                         {snare, "Snare"},
                                                         {hats, "Hats"},
                                                         {eightOhEight, "808"},
                                                         {chords, "Accords"},
                                                         {lead, "Lead"}};
        for (const auto& [id, name] : names)
            ok(std::make_unique<AddTrack>(id, name));
        ok(std::make_unique<AddBus>(drums, "Drums"));
        ok(std::make_unique<AddBus>(reverb, "Réverb"));
        for (const auto id : {kick, snare, hats})
            ok(std::make_unique<SetTrackOutput>(id, drums));
        ok(std::make_unique<SetTrackSend>(snare, reverb, -12.0));
        ok(std::make_unique<SetTrackSend>(chords, reverb, -10.0));
        ok(std::make_unique<SetTrackSend>(lead, reverb, -8.0));

        kickEq = effect(internal::equaliser, "EQ");
        ok(std::make_unique<InsertPlugin>(kick, kickEq, 0));
        saturn = theirs("Saturn");
        ok(std::make_unique<InsertPlugin>(eightOhEight, saturn, 0));
        serum = theirs("Serum");
        ok(std::make_unique<InsertPlugin>(lead, serum, 0));
        valhalla = theirs("Valhalla");
        valhalla.bypassed = true;
        ok(std::make_unique<InsertPlugin>(lead, valhalla, 1));
        drumsComp = effect(internal::compressor, "Comp");
        ok(std::make_unique<InsertPlugin>(drums, drumsComp, 0));
        room = theirs("ValhallaRoom");
        ok(std::make_unique<InsertPlugin>(reverb, room, 0));
        masterEq = effect(internal::equaliser, "EQ");
        ok(std::make_unique<InsertPlugin>(ProjectState::masterTrackId(), masterEq, 0));
    }

    void ok(std::unique_ptr<Command> command) { REQUIRE(harness.bus.execute(std::move(command)).ok()); }

    [[nodiscard]] flux::Graph graph() const
    {
        // Serum is an instrument, the others effects: the catalogue's answer.
        return flux::graphOf(harness.state, [](const PluginRef& ref) { return ref.name == "Serum"; });
    }

    Harness harness;
    TrackId kick, snare, hats, eightOhEight, chords, lead;
    TrackId drums{TrackId::generate()};
    TrackId reverb{TrackId::generate()};
    PluginInstance kickEq, saturn, serum, valhalla, drumsComp, room, masterEq;
};

std::string sum(TrackId strip)
{
    return "s:" + strip.toString() + ":sum";
}

// A link that must be there: the test stops on it rather than reading nothing.
const flux::Link& linkOf(const flux::Graph& graph, const std::string& from, const std::string& to)
{
    const auto* link = graph.link(from, to);
    REQUIRE(link != nullptr);
    return *link;
}

const flux::Node& nodeOf(const flux::Graph& graph, const std::string& id)
{
    const auto* node = graph.find(id);
    REQUIRE(node != nullptr);
    return *node;
}

} // namespace

TEST_CASE("The flux of a project of six tracks, two buses and sends: every node and every link")
{
    const Song song;
    const auto graph = song.graph();
    const auto master = ProjectState::masterTrackId();

    // Nodes: per channel, a source, a fader and the state after it, and per
    // effect an effect and the state after it; per bus and the master, a sum
    // in place of the source. Serum is the lead's source, not an effect.
    // 6 × 3 + 2 × 3 + 3 = 27, and 6 effects × 2 = 12.
    CHECK(graph.nodes.size() == 39);
    CHECK(graph.find(flux::effectNode(song.serum.id)) == nullptr);
    CHECK(nodeOf(graph, flux::sourceOf(song.lead)).label == "Lead · Serum");

    // The chains.
    CHECK(graph.link(flux::sourceOf(song.kick), flux::effectNode(song.kickEq.id)) != nullptr);
    CHECK(graph.link(flux::effectNode(song.kickEq.id), flux::afterEffect(song.kick, song.kickEq.id)) !=
          nullptr);
    CHECK(graph.link(flux::afterEffect(song.kick, song.kickEq.id), flux::faderNode(song.kick)) != nullptr);
    CHECK(graph.link(flux::faderNode(song.kick), flux::afterFader(song.kick)) != nullptr);
    CHECK(graph.link(flux::sourceOf(song.snare), flux::faderNode(song.snare)) != nullptr);
    CHECK(graph.link(sum(song.drums), flux::effectNode(song.drumsComp.id)) != nullptr);
    CHECK(graph.link(sum(master), flux::effectNode(song.masterEq.id)) != nullptr);

    // The outputs: the drums to their bus, the rest and the buses to the master.
    for (const auto id : {song.kick, song.snare, song.hats})
        CHECK(linkOf(graph, flux::afterFader(id), sum(song.drums)).kind == flux::LinkKind::output);
    for (const auto id : {song.eightOhEight, song.chords, song.lead, song.drums, song.reverb})
        CHECK(linkOf(graph, flux::afterFader(id), sum(master)).kind == flux::LinkKind::output);
    CHECK(graph.link(flux::afterFader(master), sum(master)) == nullptr);

    // The sends, with their levels.
    const auto* snareSend = graph.link(flux::afterFader(song.snare), sum(song.reverb));
    REQUIRE(snareSend != nullptr);
    CHECK(snareSend->kind == flux::LinkKind::send);
    CHECK(snareSend->levelDb == -12.0);
    CHECK(linkOf(graph, flux::afterFader(song.lead), sum(song.reverb)).levelDb == -8.0);

    // The 9 chains: one link less than their 39 nodes each, 30; 8 outputs; 3 sends.
    const auto count = [&](flux::LinkKind kind)
    {
        return std::count_if(
            graph.links.begin(), graph.links.end(), [&](const flux::Link& l) { return l.kind == kind; });
    };
    CHECK(count(flux::LinkKind::chain) == 30);
    CHECK(count(flux::LinkKind::output) == 8);
    CHECK(count(flux::LinkKind::send) == 3);

    // What the nodes say.
    CHECK(nodeOf(graph, flux::effectNode(song.valhalla.id)).bypassed);
    CHECK(nodeOf(graph, flux::effectNode(song.valhalla.id)).opaque);
    CHECK_FALSE(nodeOf(graph, flux::effectNode(song.kickEq.id)).opaque);
    CHECK(nodeOf(graph, flux::effectNode(song.kickEq.id)).label == "Égaliseur");
    CHECK(nodeOf(graph, sum(song.reverb)).label == "Σ Réverb");
    CHECK(nodeOf(graph, flux::afterFader(master)).label == "sortie");
}

TEST_CASE("Every link of the flux runs to the right, and no two nodes share a place")
{
    const Song song;
    const auto graph = song.graph();
    for (const auto& link : graph.links)
    {
        const auto* from = graph.find(link.from);
        const auto* to = graph.find(link.to);
        REQUIRE(from != nullptr);
        REQUIRE(to != nullptr);
        CHECK(from->column < to->column);
    }
    std::set<std::pair<int, int>> places;
    for (const auto& node : graph.nodes)
        CHECK(places.insert({node.column, node.row}).second);

    // The channels in track order, one row each; the drums' bus at their mean
    // row or below; the master after every bus.
    CHECK(nodeOf(graph, flux::sourceOf(song.kick)).row == 0);
    CHECK(nodeOf(graph, flux::sourceOf(song.lead)).row == 5);
    CHECK(nodeOf(graph, sum(song.drums)).row >= 1);
    for (const auto bus : {song.drums, song.reverb})
        CHECK(nodeOf(graph, sum(ProjectState::masterTrackId())).column >
              nodeOf(graph, flux::afterFader(bus)).column);
}

TEST_CASE("A link before a fader says where an effect dropped on it goes, instruments counted")
{
    const Song song;
    const auto graph = song.graph();
    // Lead: Serum (an instrument) at 0, Valhalla at 1. Before Valhalla: 1;
    // after it, before the fader: 2.
    const auto* before = graph.link(flux::sourceOf(song.lead), flux::effectNode(song.valhalla.id));
    REQUIRE(before != nullptr);
    CHECK(before->beforeFader);
    CHECK(before->insertIndex == 1);
    const auto* after =
        graph.link(flux::afterEffect(song.lead, song.valhalla.id), flux::faderNode(song.lead));
    REQUIRE(after != nullptr);
    CHECK(after->insertIndex == 2);
    CHECK_FALSE(linkOf(graph, flux::faderNode(song.lead), flux::afterFader(song.lead)).beforeFader);
}

TEST_CASE("The flux follows the project: an effect moved, a send removed, the same project the same graph")
{
    Song song;
    const auto first = song.graph();
    const auto again = song.graph();
    REQUIRE(first.nodes.size() == again.nodes.size());
    for (std::size_t index = 0; index < first.nodes.size(); ++index)
    {
        CHECK(first.nodes[index].id == again.nodes[index].id);
        CHECK(first.nodes[index].column == again.nodes[index].column);
        CHECK(first.nodes[index].row == again.nodes[index].row);
    }

    song.ok(std::make_unique<RemoveTrackSend>(song.chords, song.reverb));
    song.ok(std::make_unique<SetPluginBypassed>(song.kickEq.id, true));
    const auto after = song.graph();
    CHECK(after.link(flux::afterFader(song.chords), sum(song.reverb)) == nullptr);
    CHECK(nodeOf(after, flux::effectNode(song.kickEq.id)).bypassed);
}

TEST_CASE("No bus sits on the line of an output or a send that runs past it")
{
    const Song song;
    const auto graph = song.graph();
    // Every link out of a strip: no node of another strip between its two
    // ends, on the row it leaves from.
    for (const auto& link : graph.links)
    {
        if (link.kind == flux::LinkKind::chain)
            continue;
        const auto& from = nodeOf(graph, link.from);
        const auto& to = nodeOf(graph, link.to);
        for (const auto& node : graph.nodes)
            CHECK_FALSE((node.row == from.row && node.column > from.column && node.column < to.column));
    }
}
