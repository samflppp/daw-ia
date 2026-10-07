#include "daw/domain/flux/Graph.h"

#include "daw/domain/project/InternalEffects.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <utility>

namespace daw::domain::flux
{
namespace
{

std::string french(double value, int decimals)
{
    char out[32];
    std::snprintf(out, sizeof(out), "%.*f", decimals, value);
    std::string written{out};
    std::replace(written.begin(), written.end(), '.', ',');
    return written;
}

std::string nameOf(const PluginInstance& plugin)
{
    if (plugin.ref.format == PluginRef::internalFormat)
    {
        if (const auto* effect = findInternalEffect(plugin.ref.identifier); effect != nullptr)
            return std::string{effect->name};
    }
    return plugin.ref.name;
}

std::string faderLabel(const Track& strip)
{
    auto label = "fader " + french(strip.volumeDb, 1) + " dB";
    if (std::abs(strip.pan) > 0.005)
        label += strip.pan < 0.0 ? " · " + french(-strip.pan * 100.0, 0) + " % G"
                                 : " · " + french(strip.pan * 100.0, 0) + " % D";
    return label;
}

// One strip's chain, before it is placed: its nodes in order, head first,
// the state after its fader last.
struct Chain
{
    const Track* strip{nullptr};
    bool channel{false};
    bool master{false};
    std::vector<Node> nodes;
};

} // namespace

std::string sourceOf(TrackId strip)
{
    return "s:" + strip.toString() + ":source";
}

std::string afterEffect(TrackId strip, PluginId plugin)
{
    return "s:" + strip.toString() + ":after:" + plugin.toString();
}

std::string afterFader(TrackId strip)
{
    return "s:" + strip.toString() + ":fader";
}

std::string effectNode(PluginId plugin)
{
    return "e:" + plugin.toString();
}

std::string faderNode(TrackId strip)
{
    return "f:" + strip.toString();
}

const Node* Graph::find(const std::string& id) const noexcept
{
    const auto found =
        std::find_if(nodes.begin(), nodes.end(), [&](const Node& node) { return node.id == id; });
    return found != nodes.end() ? &*found : nullptr;
}

const Link* Graph::link(const std::string& from, const std::string& to) const noexcept
{
    const auto found = std::find_if(
        links.begin(), links.end(), [&](const Link& each) { return each.from == from && each.to == to; });
    return found != links.end() ? &*found : nullptr;
}

int Graph::columns() const noexcept
{
    int most = 0;
    for (const auto& node : nodes)
        most = std::max(most, node.column + 1);
    return most;
}

int Graph::rows() const noexcept
{
    int most = 0;
    for (const auto& node : nodes)
        most = std::max(most, node.row + 1);
    return most;
}

Graph graphOf(const ProjectState& state, const IsInstrument& isInstrument)
{
    // --- the chains
    std::vector<Chain> chains;
    const auto chainOf = [&](const Track& strip, bool channel, bool master)
    {
        Chain chain;
        chain.strip = &strip;
        chain.channel = channel;
        chain.master = master;

        Node head;
        head.kind = NodeKind::state;
        head.strip = strip.id;
        // A channel's instruments, at the head of its chain, are its source.
        std::size_t first = 0;
        if (channel)
        {
            head.id = sourceOf(strip.id);
            head.state = StateKind::source;
            head.label = strip.name;
            while (isInstrument && first < strip.plugins.size() && isInstrument(strip.plugins[first].ref))
                head.label += " · " + nameOf(strip.plugins[first++]);
        }
        else
        {
            head.id = "s:" + strip.id.toString() + ":sum";
            head.state = StateKind::sum;
            head.label = "Σ " + (master ? std::string{"Master"} : strip.name);
        }
        chain.nodes.push_back(head);

        for (std::size_t index = first; index < strip.plugins.size(); ++index)
        {
            const auto& plugin = strip.plugins[index];
            Node effect;
            effect.id = effectNode(plugin.id);
            effect.kind = NodeKind::effect;
            effect.strip = strip.id;
            effect.plugin = plugin.id;
            effect.label = nameOf(plugin);
            effect.bypassed = plugin.bypassed;
            effect.opaque = plugin.ref.format != PluginRef::internalFormat;
            chain.nodes.push_back(effect);

            Node after;
            after.id = afterEffect(strip.id, plugin.id);
            after.kind = NodeKind::state;
            after.state = StateKind::afterEffect;
            after.strip = strip.id;
            after.plugin = plugin.id;
            after.label = "après " + nameOf(plugin);
            chain.nodes.push_back(after);
        }

        Node fader;
        fader.id = faderNode(strip.id);
        fader.kind = NodeKind::fader;
        fader.strip = strip.id;
        fader.label = faderLabel(strip);
        fader.silent = !state.isAudible(strip.id);
        chain.nodes.push_back(fader);

        Node out;
        out.id = afterFader(strip.id);
        out.kind = NodeKind::state;
        out.state = StateKind::afterFader;
        out.strip = strip.id;
        out.label = master ? std::string{"sortie"} : strip.name;
        chain.nodes.push_back(out);
        return chain;
    };

    for (const auto& track : state.tracks())
        chains.push_back(chainOf(track, true, false));
    for (const auto& bus : state.buses())
        chains.push_back(chainOf(bus, false, false));
    chains.push_back(chainOf(state.master(), false, true));

    const auto masterId = ProjectState::masterTrackId();
    const auto headOf = [&](TrackId strip) -> std::string
    {
        for (const auto& chain : chains)
            if (chain.strip->id == strip)
                return chain.nodes.front().id;
        return {};
    };

    // --- the links
    Graph graph;
    std::map<std::string, std::vector<std::string>> inputs; // a head, the states that reach it
    for (const auto& chain : chains)
    {
        const auto& nodes = chain.nodes;
        // Effects are counted in the chain of the domain, instruments
        // included: an index there is what plugin.insert takes.
        std::size_t effects = 0;
        if (chain.channel && isInstrument)
            while (effects < chain.strip->plugins.size() && isInstrument(chain.strip->plugins[effects].ref))
                ++effects;
        for (std::size_t index = 0; index + 1 < nodes.size(); ++index)
        {
            Link link;
            link.from = nodes[index].id;
            link.to = nodes[index + 1].id;
            link.kind = LinkKind::chain;
            link.strip = chain.strip->id;
            // From a state into an effect or the fader: where an effect
            // dropped there would go.
            if (nodes[index].kind == NodeKind::state && nodes[index + 1].kind != NodeKind::state)
            {
                link.beforeFader = true;
                link.insertIndex = effects;
            }
            if (nodes[index + 1].kind == NodeKind::effect)
                ++effects;
            graph.links.push_back(link);
        }
        if (chain.master)
            continue;

        const auto destination = chain.strip->output.isNil() ? masterId : chain.strip->output;
        Link output;
        output.from = nodes.back().id;
        output.to = headOf(destination);
        output.kind = LinkKind::output;
        output.strip = chain.strip->id;
        graph.links.push_back(output);
        inputs[output.to].push_back(output.from);

        for (const auto& send : chain.strip->sends)
        {
            Link link;
            link.from = nodes.back().id;
            link.to = headOf(send.bus);
            link.kind = LinkKind::send;
            link.levelDb = send.levelDb;
            link.strip = chain.strip->id;
            if (link.to.empty())
                continue;
            graph.links.push_back(link);
            inputs[link.to].push_back(link.from);
        }
    }

    // --- the columns: the longest way from a source. A chain's nodes follow
    // its head one column each; a head waits for every state that reaches it.
    std::map<std::string, int> column;
    std::set<std::size_t> placed;
    for (std::size_t pass = 0; pass <= chains.size() && placed.size() < chains.size(); ++pass)
    {
        for (std::size_t index = 0; index < chains.size(); ++index)
        {
            if (placed.contains(index))
                continue;
            const auto& head = chains[index].nodes.front().id;
            int start = 0;
            bool ready = true;
            for (const auto& from : inputs[head])
            {
                const auto known = column.find(from);
                if (known == column.end())
                {
                    ready = false;
                    break;
                }
                start = std::max(start, known->second + 1);
            }
            // The last pass places what a loop would leave out, rather than
            // nothing: the domain refuses a loop, this does not depend on it.
            if (!ready && pass < chains.size())
                continue;
            for (std::size_t node = 0; node < chains[index].nodes.size(); ++node)
                column[chains[index].nodes[node].id] = start + static_cast<int>(node);
            placed.insert(index);
        }
    }

    // --- the rows: a channel each, in track order; a bus and the master at
    // the mean row of what reaches them, below anything already in the way —
    // a node, or the way out of a strip placed before: an output or a send
    // keeps its row free up to where it arrives, so that no bus sits on a
    // line that runs past it.
    std::map<std::string, int> row;
    std::set<std::pair<int, int>> taken; // (column, row)
    const auto place = [&](const Chain& chain, int wanted)
    {
        const auto first = column[chain.nodes.front().id];
        const auto last = column[chain.nodes.back().id];
        auto at = std::max(0, wanted);
        const auto free = [&](int candidate)
        {
            for (int each = first; each <= last; ++each)
                if (taken.contains({each, candidate}))
                    return false;
            return true;
        };
        while (!free(at))
            ++at;
        for (const auto& node : chain.nodes)
        {
            row[node.id] = at;
            taken.insert({column[node.id], at});
        }
        for (const auto& link : graph.links)
        {
            if (link.kind == LinkKind::chain || link.from != chain.nodes.back().id)
                continue;
            for (auto each = last + 1; each < column[link.to]; ++each)
                taken.insert({each, at});
        }
    };

    int channels = 0;
    for (const auto& chain : chains)
        if (chain.channel)
            place(chain, channels++);

    // Buses in the order their columns allow, the master last.
    std::vector<const Chain*> mixing;
    for (const auto& chain : chains)
        if (!chain.channel)
            mixing.push_back(&chain);
    std::stable_sort(mixing.begin(),
                     mixing.end(),
                     [&](const Chain* a, const Chain* b)
                     { return column[a->nodes.front().id] < column[b->nodes.front().id]; });
    for (const auto* chain : mixing)
    {
        const auto& from = inputs[chain->nodes.front().id];
        int wanted = channels;
        if (!from.empty())
        {
            double sum = 0.0;
            for (const auto& each : from)
                sum += row[each];
            wanted = static_cast<int>(std::lround(sum / static_cast<double>(from.size())));
        }
        place(*chain, wanted);
    }

    for (auto& chain : chains)
        for (auto& node : chain.nodes)
        {
            node.column = column[node.id];
            node.row = row[node.id];
            graph.nodes.push_back(node);
        }
    return graph;
}

} // namespace daw::domain::flux
