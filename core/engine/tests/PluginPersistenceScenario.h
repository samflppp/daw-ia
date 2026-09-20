#pragma once

#include <juce_core/juce_core.h>

namespace daw::testing
{

// What the child process does: build an engine, host the CLAP fixture through
// the bus, turn its gain up, capture its opaque state into the project's own
// content store, play notes, measure the rendered signal, write that number
// next to the project, and close everything.
//
// It has to be a separate process. A plugin instance kept alive in memory
// would restore itself from its own state and prove nothing about the bytes on
// disk; the only honest measurement is one taken by a process that never saw
// the first one.
[[nodiscard]] int writePluginProject(const juce::File& projectFolder, const juce::File& rmsFile);

} // namespace daw::testing
