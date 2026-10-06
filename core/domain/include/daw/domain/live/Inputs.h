#pragma once

#include <string>
#include <vector>

namespace daw::domain::live
{

// Which MIDI input plays as which source of the router (S23). The inputs of
// the machine come and go — a keyboard unplugged, plugged back —; each keeps
// its source while it is there, a new one takes the first free source, and
// one that went away is released: what it held stops.
struct InputSlot
{
    std::string identifier;
    int source{0};

    friend bool operator==(const InputSlot& lhs, const InputSlot& rhs) = default;
};

struct InputChange
{
    std::vector<InputSlot> kept;      // still there, same source
    std::vector<InputSlot> added;     // new, with the source given
    std::vector<InputSlot> removed;   // gone: release their source
    std::vector<std::string> waiting; // no source left for them
};

// `firstSource` and `count`: the sources the router keeps for MIDI inputs.
[[nodiscard]] InputChange follow(const std::vector<InputSlot>& listened,
                                 const std::vector<std::string>& present,
                                 int firstSource,
                                 int count);

} // namespace daw::domain::live
