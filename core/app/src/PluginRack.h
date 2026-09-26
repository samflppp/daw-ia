#pragma once

#include "PluginWindow.h"
#include "daw/engine/PluginCatalogue.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/model/PluginHost.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <map>
#include <memory>
#include <string>

namespace daw::app
{

// The plugins of this machine, and the windows they draw into.
//
// Everything Tracktion-shaped about hosting stops here, like the clock stops
// the playhead: the chain panel asks by domain identifier and never sees an
// Edit, a description or a window.
//
// One window per plugin, kept in a map: a user who opens a compressor, then a
// synth, expects both to stay open, and closing one must not close the other.
class PluginRack final : public ui::PluginHost
{
public:
    PluginRack(tracktion::Edit& edit, engine::PluginCatalogue& catalogue, const ui::Tokens& tokens);
    ~PluginRack() override;

    [[nodiscard]] std::vector<domain::PluginRef> available() const override;
    [[nodiscard]] bool isInstalled(const domain::PluginRef& ref) const override;
    [[nodiscard]] bool isInstrument(const domain::PluginRef& ref) const override;

    [[nodiscard]] bool hasEditor(domain::PluginId pluginId) const override;
    [[nodiscard]] bool editorIsOpen(domain::PluginId pluginId) const override;

    void openEditor(domain::PluginId pluginId) override;
    void closeEditor(domain::PluginId pluginId) override;

    void rescan() override;

    // Closes every window, before the Edit that holds the plugins goes.
    void closeAll();

private:
    // The plugin the projection created for this domain identifier. Null when
    // the projection has not run yet, or when the plugin is not installed.
    [[nodiscard]] tracktion::Plugin* find(domain::PluginId pluginId) const;

    tracktion::Edit& edit_;
    engine::PluginCatalogue& catalogue_;
    const ui::Tokens& tokens_;

    std::map<std::string, std::unique_ptr<PluginWindow>> windows_;
};

} // namespace daw::app
