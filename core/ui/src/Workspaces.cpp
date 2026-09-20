#include "daw/ui/Workspaces.h"

#include <DawWorkspacesData.h>

#include <algorithm>

namespace daw::ui
{

const Workspaces& Workspaces::builtIn()
{
    static const Workspaces workspaces;
    return workspaces;
}

Workspaces::Workspaces()
{
    for (int index = 0; index < DawWorkspacesData::namedResourceListSize; ++index)
    {
        int size = 0;
        const auto* name = DawWorkspacesData::namedResourceList[index];
        const auto* data = DawWorkspacesData::getNamedResource(name, size);

        if (data == nullptr || size <= 0)
        {
            rejected_.push_back(juce::String(name) + ": empty resource");
            continue;
        }

        auto manifest = WorkspaceManifest::parse(std::string_view{data, static_cast<std::size_t>(size)});
        if (!manifest)
        {
            rejected_.push_back(juce::String(name) + ": " + juce::String(manifest.error().message));
            continue;
        }

        manifests_.push_back(std::move(manifest).value());
    }

    // The order on disk is the order of a directory listing, which is not an
    // order anyone chose. The switch shows them alphabetically instead, so it
    // does not reshuffle when a manifest is added.
    std::sort(manifests_.begin(),
              manifests_.end(),
              [](const WorkspaceManifest& lhs, const WorkspaceManifest& rhs) { return lhs.id < rhs.id; });
}

const WorkspaceManifest* Workspaces::find(std::string_view id) const noexcept
{
    const auto found = std::find_if(manifests_.begin(),
                                    manifests_.end(),
                                    [id](const WorkspaceManifest& manifest) { return manifest.id == id; });

    return found == manifests_.end() ? nullptr : &(*found);
}

} // namespace daw::ui
