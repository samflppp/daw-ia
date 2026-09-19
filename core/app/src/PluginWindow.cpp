#include "PluginWindow.h"

namespace daw::app
{
namespace
{

juce::AudioProcessorEditor* createEditorFor(tracktion::Plugin& plugin)
{
    auto* external = dynamic_cast<tracktion::ExternalPlugin*>(&plugin);
    if (external == nullptr)
        return nullptr;

    auto* instance = external->getAudioPluginInstance();
    if (instance == nullptr || !instance->hasEditor())
        return nullptr;

    return instance->createEditorAndMakeActive();
}

} // namespace

bool PluginWindow::hasEditor(tracktion::Plugin& plugin)
{
    auto* external = dynamic_cast<tracktion::ExternalPlugin*>(&plugin);
    if (external == nullptr)
        return false;

    auto* instance = external->getAudioPluginInstance();
    return instance != nullptr && instance->hasEditor();
}

PluginWindow::PluginWindow(tracktion::Plugin& plugin, const ui::Tokens& tokens)
    : juce::DocumentWindow(
          plugin.getName(), tokens.colour("color.surface.base"), juce::DocumentWindow::closeButton)
    , plugin_{&plugin}
{
    setUsingNativeTitleBar(true);

    // Owned by the window, and deleted with it: a juce::AudioProcessorEditor
    // tells its processor it is going away from its own destructor, so this is
    // the one arrangement that cannot leave the plugin holding a dead editor.
    if (auto* editor = createEditorFor(plugin); editor != nullptr)
    {
        setContentOwned(editor, true);

        // The plugin decides its own size, so the window follows it instead of
        // imposing one. Resizable only when the editor itself is.
        setResizable(editor->isResizable(), false);
    }

    centreAroundComponent(nullptr, getWidth(), getHeight());
    setVisible(true);
}

PluginWindow::~PluginWindow()
{
    // The editor goes while the plugin is still alive, never the other way
    // round: the reference to the plugin is held until this line has run.
    clearContentComponent();
}

void PluginWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}

} // namespace daw::app
