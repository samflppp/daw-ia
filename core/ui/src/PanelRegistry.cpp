#include "daw/ui/PanelRegistry.h"

#include "daw/ui/panels/PlaceholderPanel.h"

#include <algorithm>
#include <utility>

namespace daw::ui
{

PanelRegistry PanelRegistry::withBuiltinPanels()
{
    PanelRegistry registry;

    // Nothing yet: the panels of the beatmaker land one by one, and until one
    // exists its identifier resolves to the placeholder. The registry is the
    // only place that has to change when it does.
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
