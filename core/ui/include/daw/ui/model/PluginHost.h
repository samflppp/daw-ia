#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/project/ProjectState.h"

#include <vector>

namespace daw::ui
{

// What the chain panel cannot know by itself.
//
// Which plugins are installed on this machine is not project state: it is not
// journalled and it differs from one computer to the next. Opening an editor
// is not project state either — a window is not an edit, and nothing about it
// belongs in the journal or in an undo.
//
// Both stop here, for the same reason TransportClock exists: core/ui never
// includes Tracktion. The panel asks; the application answers over the Edit
// and the catalogue.
class PluginHost
{
public:
    PluginHost() = default;
    virtual ~PluginHost() = default;

    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;
    PluginHost(PluginHost&&) = delete;
    PluginHost& operator=(PluginHost&&) = delete;

    // The plugins this machine knows about, as the domain names them.
    [[nodiscard]] virtual std::vector<domain::PluginRef> available() const = 0;

    // False when a project names a plugin that is not installed here. The
    // chain keeps the slot and says so, because silently dropping it would
    // lose the plugin the day the project goes back to the machine that has
    // it.
    [[nodiscard]] virtual bool isInstalled(const domain::PluginRef& ref) const = 0;

    [[nodiscard]] virtual bool hasEditor(domain::PluginId pluginId) const = 0;
    [[nodiscard]] virtual bool editorIsOpen(domain::PluginId pluginId) const = 0;

    virtual void openEditor(domain::PluginId pluginId) = 0;
    virtual void closeEditor(domain::PluginId pluginId) = 0;

    // Scans the machine again. Long, so it is asked for, never done on its
    // own behind a click the user did not make.
    virtual void rescan() = 0;
};

} // namespace daw::ui
