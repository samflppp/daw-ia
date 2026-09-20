#include "daw/ui/PanelRegistry.h"

#include "daw/ui/panels/HistoryPanel.h"
#include "daw/ui/panels/PianoRollPanel.h"
#include "daw/ui/panels/PlaceholderPanel.h"
#include "daw/ui/panels/PluginChainPanel.h"
#include "daw/ui/panels/TrackListPanel.h"
#include "daw/ui/panels/TransportPanel.h"

#include <algorithm>
#include <utility>

namespace daw::ui
{

PanelRegistry PanelRegistry::withBuiltinPanels()
{
    PanelRegistry registry;

    // The panels of the beatmaker land one by one. Until one exists its
    // identifier resolves to the placeholder, and this is the only place that
    // has to change when it arrives.
    registry.add("transport",
                 [](const PanelContext& context) { return std::make_unique<TransportPanel>(context); });
    registry.add("tracks",
                 [](const PanelContext& context) { return std::make_unique<TrackListPanel>(context); });
    registry.add("piano_roll",
                 [](const PanelContext& context) { return std::make_unique<PianoRollPanel>(context); });
    registry.add("plugin_chain",
                 [](const PanelContext& context) { return std::make_unique<PluginChainPanel>(context); });
    registry.add("history",
                 [](const PanelContext& context) { return std::make_unique<HistoryPanel>(context); });

    return registry;
}

void PanelRegistry::add(std::string id, Factory factory)
{
    entries_.push_back(Entry{std::move(id), std::move(factory)});
}

bool PanelRegistry::contains(std::string_view id) const noexcept
{
    return std::any_of(entries_.begin(), entries_.end(), [id](const Entry& entry) { return entry.id == id; });
}

std::unique_ptr<juce::Component> PanelRegistry::create(const PanelContext& context) const
{
    for (const auto& entry : entries_)
    {
        if (entry.id == context.id && entry.factory != nullptr)
            return entry.factory(context);
    }

    return std::make_unique<PlaceholderPanel>(context);
}

} // namespace daw::ui
