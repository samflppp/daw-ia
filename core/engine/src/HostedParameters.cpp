#include "HostedParameters.h"

namespace daw::engine
{

std::vector<std::pair<std::string, tracktion::AutomatableParameter*>>
hostedParameters(tracktion::Plugin& plugin)
{
    std::vector<std::pair<std::string, tracktion::AutomatableParameter*>> found;

    auto* external = dynamic_cast<tracktion::ExternalPlugin*>(&plugin);
    if (external == nullptr)
        return found;

    auto* instance = external->getAudioPluginInstance();
    if (instance == nullptr)
        return found;

    const auto& hosted = instance->getParameters();
    const auto total = plugin.getNumAutomatableParameters();

    // Tracktion's own parameters come first, the plugin's after them, in
    // declaration order. The offset is therefore the difference between the two
    // counts, and a mismatch means the assumption no longer holds — in which
    // case nothing is reported rather than the wrong parameter.
    const auto offset = total - hosted.size();
    if (offset < 0)
        return found;

    found.reserve(static_cast<std::size_t>(hosted.size()));

    for (int index = 0; index < hosted.size(); ++index)
    {
        auto parameter = plugin.getAutomatableParameter(offset + index);
        if (parameter == nullptr)
            continue;

        std::string paramId;
        if (const auto* withId = dynamic_cast<const juce::HostedAudioProcessorParameter*>(hosted[index]);
            withId != nullptr)
            paramId = withId->getParameterID().toStdString();
        else
            paramId = std::to_string(index);

        found.emplace_back(std::move(paramId), parameter.get());
    }

    return found;
}

tracktion::AutomatableParameter* hostedParameter(tracktion::Plugin& plugin, const std::string& paramId)
{
    for (const auto& [id, parameter] : hostedParameters(plugin))
    {
        if (id == paramId)
            return parameter;
    }
    return nullptr;
}

} // namespace daw::engine
