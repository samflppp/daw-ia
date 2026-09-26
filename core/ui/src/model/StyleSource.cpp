#include "daw/ui/model/StyleSource.h"

#include "daw/domain/serialization/Json.h"

#include <juce_core/juce_core.h>

namespace daw::ui
{
namespace
{

domain::generation::StyleModel load()
{
    const auto chosen = juce::SystemStats::getEnvironmentVariable("DAW_IA_STYLE_MODEL", {});
    if (chosen == "repli")
    {
        juce::Logger::writeToLog("generation: style model repli (DAW_IA_STYLE_MODEL)");
        return domain::generation::StyleModel::fallback();
    }

    const auto file = chosen.isNotEmpty()
                          ? juce::File{chosen}
                          : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                .getChildFile("DAW IA")
                                .getChildFile("generation")
                                .getChildFile("markov.json");

    if (!file.existsAsFile())
    {
        juce::Logger::writeToLog("generation: style model repli (no " + file.getFullPathName() + ")");
        return domain::generation::StyleModel::fallback();
    }

    const auto text = file.loadFileAsString();
    auto value = domain::json::read(text.toStdString());
    if (!value)
    {
        juce::Logger::writeToLog("generation: style model repli (" + file.getFullPathName() + ": " +
                                 juce::String{value.error().message} + ")");
        return domain::generation::StyleModel::fallback();
    }

    auto model = domain::generation::StyleModel::fromValue(value.value());
    if (!model)
    {
        juce::Logger::writeToLog("generation: style model repli (" + file.getFullPathName() + ": " +
                                 juce::String{model.error().message} + ")");
        return domain::generation::StyleModel::fallback();
    }

    juce::Logger::writeToLog("generation: style model " + juce::String{model.value().origin()} + " (" +
                             file.getFullPathName() + ")");
    return std::move(model).value();
}

} // namespace

const domain::generation::StyleModel& styleModel()
{
    static const auto model = load();
    return model;
}

} // namespace daw::ui
