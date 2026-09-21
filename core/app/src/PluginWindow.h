#pragma once

#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>

namespace daw::app
{

// The window a plugin draws into.
//
// Nothing but the plugin's own editor: a margin, a toolbar or a title strip
// inside this window would shift a plugin's drawing away from where the plugin
// thinks it is, and a VST3 or CLAP editor positions itself from the origin of
// the window it was given.
//
// The window owns nothing of the plugin. Closing it destroys the editor and
// leaves the plugin running, which is what a user expects: closing a synth's
// window does not stop the synth.
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow(tracktion::Plugin& plugin, const ui::Tokens& tokens);
    ~PluginWindow() override;

    // Called when the user closes the window, so the owner can forget it.
    std::function<void()> onClose;

    void closeButtonPressed() override;
    void childBoundsChanged(juce::Component* child) override;

    // True when the plugin gave an editor to show. A plugin without one is not
    // an error: the window simply does not open.
    [[nodiscard]] static bool hasEditor(tracktion::Plugin& plugin);

private:
    // Puts the window inside the usable area of the display, whatever size the
    // plugin asked for.
    void placeOnScreen();

    tracktion::Plugin::Ptr plugin_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginWindow)
};

} // namespace daw::app
