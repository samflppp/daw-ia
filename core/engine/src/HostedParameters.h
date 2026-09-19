#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <string>
#include <utility>
#include <vector>

namespace daw::engine
{

// The parameters that belong to a hosted plugin, and only those.
//
// A Tracktion plugin exposes more automatable parameters than the plugin does:
// an ExternalPlugin adds "dry level" and "wet level" of its own, in front of the
// plugin's. Two consequences, and both would be bugs:
//
//   index      getAutomatableParameter(0) is Tracktion's dry level, not the
//              first parameter of the plugin.
//
//   name       a plugin whose own parameter id happens to be "dry level" would
//              be addressed through getAutomatableParameterByID() as Tracktion's
//              parameter instead of its own.
//
// So the plugin's parameters are found through the wrapped juce instance, which
// is the only place that knows their real identifiers, and matched to the
// Tracktion parameters by position: Tracktion appends the plugin's parameters
// after its own, in the order the plugin declares them.
//
// The pair is {the format's own parameter id, the Tracktion parameter}. That id
// is what goes into PluginParam::paramId, therefore into the journal.
[[nodiscard]] std::vector<std::pair<std::string, tracktion::AutomatableParameter*>>
hostedParameters(tracktion::Plugin& plugin);

// The one parameter of the plugin with that identifier, or nullptr.
[[nodiscard]] tracktion::AutomatableParameter* hostedParameter(tracktion::Plugin& plugin,
                                                               const std::string& paramId);

} // namespace daw::engine
