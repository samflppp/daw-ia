#pragma once

#include <cstdint>
#include <vector>

namespace daw::app
{

// The processes the DAW starts, and theirs (S26).
//
// A service is started through `uv run`, which starts Python, which on
// Windows is a venv launcher starting Python again: killing the process
// juce::ChildProcess knows (uv) left the two Pythons running. Found in S26:
// copilots of earlier runs alive half an hour later, one of them answering
// the next copilot's socket.
//
// So the process a service was started as is found right after its start,
// and killed with every process under it. And the DAW is put, at start, in a
// Windows job that kills every process it started, and theirs, when the DAW's
// own process ends — a crash included.
namespace processes
{

using Id = std::uint32_t;

// Once, at start: every process started from now on dies with this one.
void dieWithThisProcess();

// The processes this one started that are alive now.
[[nodiscard]] std::vector<Id> children();

// The one in `after` that was not in `before`: the process just started.
// Zero when there is none, or more than one.
[[nodiscard]] Id startedBetween(const std::vector<Id>& before, const std::vector<Id>& after);

// Kills `root` and every process under it, the deepest first.
void killTree(Id root);

} // namespace processes
} // namespace daw::app
