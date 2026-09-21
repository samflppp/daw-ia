#include "PluginRack.h"

namespace daw::app
{
namespace
{

// The property the projector writes on a projected plugin, so that a domain
// identifier finds the Tracktion plugin that answers for it.
const juce::Identifier domainPluginIdProperty{"dawDomainPluginId"};

} // namespace

PluginRack::PluginRack(tracktion::Edit& edit, engine::PluginCatalogue& catalogue, const ui::Tokens& tokens)
    : edit_(edit)
    , catalogue_(catalogue)
    , tokens_(tokens)
{
}

PluginRack::~PluginRack()
{
    closeAll();
}

std::vector<domain::PluginRef> PluginRack::available() const
{
    std::vector<domain::PluginRef> refs;

    for (const auto& description : catalogue_.descriptions())
        refs.push_back(engine::PluginCatalogue::refFor(description));

    return refs;
}

bool PluginRack::isInstalled(const domain::PluginRef& ref) const
{
    return catalogue_.find(ref).has_value();
}

tracktion::Plugin* PluginRack::find(domain::PluginId pluginId) const
{
    const auto wanted = juce::String(pluginId.toString());

    for (auto* track : tracktion::getAudioTracks(edit_))
    {
        if (track == nullptr)
            continue;

        for (auto plugin : track->pluginList.getPlugins())
        {
            if (plugin != nullptr && plugin->state.getProperty(domainPluginIdProperty).toString() == wanted)
                return plugin;
        }
    }

    return nullptr;
}

bool PluginRack::hasEditor(domain::PluginId pluginId) const
{
    auto* plugin = find(pluginId);
    return plugin != nullptr && PluginWindow::hasEditor(*plugin);
}

bool PluginRack::editorIsOpen(domain::PluginId pluginId) const
{
    return windows_.find(pluginId.toString()) != windows_.end();
}

void PluginRack::openEditor(domain::PluginId pluginId)
{
    const auto key = pluginId.toString();

    if (auto existing = windows_.find(key); existing != windows_.end())
    {
        existing->second->toFront(true);
        return;
    }

    auto* plugin = find(pluginId);
    if (plugin == nullptr)
    {
        juce::Logger::writeToLog("plugin window: no plugin " + juce::String(key) + " in the Edit");
        return;
    }

    if (!PluginWindow::hasEditor(*plugin))
    {
        juce::Logger::writeToLog("plugin window: " + plugin->getName() + " reports no editor");
        return;
    }

    auto window = std::make_unique<PluginWindow>(*plugin, tokens_);

    // The window tells the rack when the user closes it, so that clicking the
    // slot again opens it instead of doing nothing.
    window->onClose = [this, key] { windows_.erase(key); };
    windows_.emplace(key, std::move(window));
}

void PluginRack::closeEditor(domain::PluginId pluginId)
{
    windows_.erase(pluginId.toString());
}

void PluginRack::rescan()
{
    const auto report = catalogue_.scan();

    juce::Logger::writeToLog("plugin scan: " + juce::String(report.scanned) + " files, " +
                             juce::String(report.added) + " added, " + juce::String(report.blacklisted) +
                             " blacklisted, " + juce::String(catalogue_.descriptions().size()) +
                             " known in total");

    if (const auto saved = catalogue_.save(); !saved)
        juce::Logger::writeToLog("plugin list not saved: " + juce::String(saved.error().message));
}

void PluginRack::closeAll()
{
    windows_.clear();
}

} // namespace daw::app
