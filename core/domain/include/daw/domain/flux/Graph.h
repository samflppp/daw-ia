#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace daw::domain::flux
{

// The audio flux (S24): the way the sound goes through the whole song, as a
// graph, after the Flow of Dataiku — there, datasets joined by recipes;
// here, states of the sound joined by effects.
//
// Computed from ProjectState and nothing else: a reading of the project,
// never stored in it, like a role guessed for the mix. Where it is drawn is
// a matter of the screen; where it goes is the routing, and only that.
//
// For each strip — a channel, a bus, the master:
//
//   [state: source] → (effect) → [state: after it] → (effect) → … →
//   (fader) → [state: after the fader] ⇒ its output, a bus or the master
//                                      ⇢ its sends, each to a bus, a level
//
// A channel's source is its instrument or its recordings; a bus's and the
// master's is the sum of what reaches it. The master's state after its
// fader is what leaves the machine. A state is where the sound can be seen
// and heard (the taps of the engine are at those places); an effect is one
// plugin, the DAW's or the person's, the same node for both.
//
// A channel's recordings do not go through the person's plugins: they play
// on a track of their own in the engine, which carries the DAW's effects only
// (S20; the person's would be a second instance, its state drifting from the
// first). When a channel has recordings and a plugin of the person's, the
// flux says so: a second branch, « enregistrements », from the recordings
// through the DAW's effects alone, joins the channel at its fader; the first
// is then the instrument's alone.
enum class NodeKind
{
    state,
    effect,
    fader
};

enum class StateKind
{
    source,      // a channel's instrument or recordings
    sum,         // a bus or the master: what reaches it, added
    afterEffect, // after the effect `plugin`
    afterFader,  // after the strip's fader and pan; for the master, the way out
};

// Which of a channel's two ways a node is on, when it has two.
enum class Path
{
    both,       // one way: a channel without that split, a bus, the master
    instrument, // what the instrument plays, through every plugin
    recordings  // what the recordings play, through the DAW's effects alone
};

struct Node
{
    // Stable across projections, built from identifiers only: a strip's
    // states « s:<strip>:source », « s:<strip>:sum », « s:<strip>:after:<plugin> »,
    // « s:<strip>:fader »; an effect « e:<plugin> »; a fader « f:<strip> ».
    std::string id;
    NodeKind kind{NodeKind::state};
    StateKind state{StateKind::source};
    TrackId strip;
    PluginId plugin; // the effect, or the effect a state comes after
    std::string label;
    bool bypassed{false}; // an effect that does nothing
    bool opaque{false};   // a plugin of the person's: its sound is seen, never its settings
    bool silent{false};   // a fader whose strip is not heard (muted, or another soloed)
    Path path{Path::both};

    // Where it is drawn: a column from the left, a row from the top. Integers,
    // in a grid; the screen turns them into pixels.
    int column{0};
    int row{0};
};

enum class LinkKind
{
    chain,  // within a strip
    output, // a strip's output, to a bus or the master
    send    // a send after the fader, to a bus
};

struct Link
{
    std::string from;
    std::string to;
    LinkKind kind{LinkKind::chain};
    double levelDb{0.0}; // a send's level

    // For a link inside a chain, before the fader: the strip, and the index
    // an effect dropped on this link gets in that strip's chain.
    TrackId strip;
    std::size_t insertIndex{0};
    bool beforeFader{false};
};

struct Graph
{
    std::vector<Node> nodes;
    std::vector<Link> links;

    [[nodiscard]] const Node* find(const std::string& id) const noexcept;
    [[nodiscard]] const Link* link(const std::string& from, const std::string& to) const noexcept;
    [[nodiscard]] int columns() const noexcept;
    [[nodiscard]] int rows() const noexcept;
};

// The node identifiers, as graphOf builds them.
[[nodiscard]] std::string sourceOf(TrackId strip);
[[nodiscard]] std::string afterEffect(TrackId strip, PluginId plugin);
[[nodiscard]] std::string afterFader(TrackId strip);
[[nodiscard]] std::string effectNode(PluginId plugin);
[[nodiscard]] std::string faderNode(TrackId strip);
// The recordings' branch: its source, an effect on it, the state after it.
[[nodiscard]] std::string recordingsOf(TrackId strip);
[[nodiscard]] std::string effectOnRecordings(PluginId plugin);
[[nodiscard]] std::string afterEffectOnRecordings(TrackId strip, PluginId plugin);

// Whether a plugin is an instrument: the domain cannot tell a VST3 synth
// from a VST3 effect, the catalogue of the machine can. An instrument at the
// head of a channel's chain is its source, not an effect: the source state is
// what it plays.
using IsInstrument = std::function<bool(const PluginRef&)>;

// The graph of the project. Its layout: a node's column is the length of the
// longest way from a source to it, so that every link runs to the right; a
// channel has a row of its own, in the order of the tracks; a bus and the
// master sit at the mean row of what reaches them, pushed down past any
// row already taken in their columns. The same project gives the same graph.
[[nodiscard]] Graph graphOf(const ProjectState& state, const IsInstrument& isInstrument = {});

} // namespace daw::domain::flux
