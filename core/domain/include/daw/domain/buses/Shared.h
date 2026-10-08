#pragma once

#include "daw/domain/buses/Kind.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace daw::domain::buses
{

// The smart buses (S24): the same effect on several tracks, shared on one
// bus — decided with the founder on 6 October 2026.
//
//   by a send   a reverb or a delay (as Kind.h recognises the plugin), last
//               in its chain on at least two tracks: a bus carries one
//               instance, each track sends to it at 0 dB. Never one of the
//               DAW's effects, never an instrument, never an effect another
//               follows (moving it would change the order).
//   by a group  the DAW's equaliser, or its compressor, with exactly the same
//               parameters, last in the chains of tracks that go to the same
//               place and send nowhere: they go out to a bus that carries it
//               instead. An equaliser is linear: the same sound, which the
//               dry run proves (under -90 dB). A compressor on the sum is not
//               a compressor on each track: « compression de groupe : le son
//               change », and the dry run says by how much.
//
// « The same »: the same plugin and the same state digest, or, for the DAW's
// effects, the same parameters. « Close » (sends only): the same plugin and
// its parameters within 0.02, but another state — proposed, and said.
//
// A plugin that cannot be told (Kind.h, since 8 October 2026) is proposed as a
// question, with what its name suggests: answered, it is proposed again as
// what the person said. Never tried before it is answered.

enum class Way
{
    send,
    group
};

enum class Certainty
{
    same,
    close
};

struct Shared
{
    Way way{Way::send};
    Certainty certainty{Certainty::same};
    PluginInstance plugin;         // the instance the bus gets (a copy of the first)
    std::vector<TrackId> tracks;   // two or more, in track order
    std::vector<PluginId> removed; // each track's instance, taken off
    TrackId destination;           // a group's way out: where the tracks went (nil, the master)
    std::string sentence;          // French, what is proposed and why

    KnownBy by{KnownBy::catalogue}; // how the effect's kind is known; unknown: a question
    std::optional<Kind> suggested;  // a question's suggestion, from the name

    [[nodiscard]] bool toAsk() const noexcept { return by == KnownBy::unknown; }
};

// What a plugin of the person's is (Kind.h): the catalogue, the name, the
// person's answer.
using Recognise = std::function<Recognition(const PluginRef&)>;

[[nodiscard]] std::vector<Shared> propose(const ProjectState& state, const Recognise& recognise);

// The commands that make it, existing verbs only, the identifiers given by
// the caller: bus.add, plugin.insert on the bus, then for each track
// track.set_send (or track.set_output) and plugin.remove; a group's bus goes
// out where its tracks went.
[[nodiscard]] std::vector<std::unique_ptr<Command>>
compile(const Shared& shared, TrackId bus, const std::string& busName, PluginId plugin);

} // namespace daw::domain::buses
