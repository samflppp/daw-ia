#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/project/ProjectState.h"

#include <string>
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

    // True for an instrument, false for an effect or a plugin not installed
    // here. The channel rack offers only instruments to start a channel with.
    [[nodiscard]] virtual bool isInstrument(const domain::PluginRef& ref) const = 0;

    [[nodiscard]] virtual bool hasEditor(domain::PluginId pluginId) const = 0;
    [[nodiscard]] virtual bool editorIsOpen(domain::PluginId pluginId) const = 0;

    virtual void openEditor(domain::PluginId pluginId) = 0;
    virtual void closeEditor(domain::PluginId pluginId) = 0;

    // Scans the machine again. Long, so it is asked for, never done on its
    // own behind a click the user did not make.
    virtual void rescan() = 0;

    // The preset the plugin says it has loaded (S17), for naming a track.
    // Empty when it says nothing: many synths show their preset in their own
    // window only, and a CLAP plugin answers with its own name, which is not
    // a preset. Read, never stored: the preset lives in the plugin's blob.
    [[nodiscard]] virtual std::string presetName(domain::PluginId pluginId) const
    {
        static_cast<void>(pluginId);
        return {};
    }
};

} // namespace daw::ui
