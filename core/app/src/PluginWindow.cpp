#include "PluginWindow.h"

#include "DisplayMode.h"

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

    // Visible first: the native frame only exists once the window is on the
    // desktop, and its height is what decides whether the title bar ends up
    // above the top of the screen.
    setVisible(true);

    // Léger on this machine: the plugin's window draws in software too.
    if (auto* peer = getPeer(); peer != nullptr)
        display::applyTo(*peer);
    placeOnScreen();
}

void PluginWindow::placeOnScreen()
{
    // Some editors are larger than the screen they are opened on -- Vital asks
    // for 1489 by 901 on a 1280 by 720 display. Centring such a window puts
    // its title bar above the top of the screen and its close button out of
    // reach, so it is clamped into the usable area instead, top-left first.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (display == nullptr)
    {
        centreAroundComponent(nullptr, getWidth(), getHeight());
        return;
    }

    const auto available = display->userBounds.toNearestInt();

    // A plugin that cannot be resized keeps the size it asked for, even when
    // that size is larger than the screen. Shrinking the window would leave
    // the plugin drawing its own size inside a bigger frame, and the rest of
    // the frame is the black band that was reported -- the window would be
    // wrong in a way the user cannot fix, instead of merely too big.
    const auto width = isResizable() ? juce::jmin(getWidth(), available.getWidth()) : getWidth();
    const auto height = isResizable() ? juce::jmin(getHeight(), available.getHeight()) : getHeight();

    // Whatever the size, the top-left corner stays reachable: a title bar
    // above the top of the screen is a window that cannot be closed. The frame
    // is not part of this component, so its height has to be asked for and
    // added, or the bar lands exactly that far off the screen.
    auto top = available.getY();
    auto left = available.getX();

    if (auto* peer = getPeer())
    {
        if (const auto frame = peer->getFrameSizeIfPresent())
        {
            top += frame->getTop();
            left += frame->getLeft();
        }
    }

    const auto x = juce::jmax(left, available.getCentreX() - width / 2);
    const auto y = juce::jmax(top, available.getCentreY() - height / 2);

    setBounds(x, y, width, height);
}

void PluginWindow::childBoundsChanged(juce::Component* child)
{
    juce::DocumentWindow::childBoundsChanged(child);

    // The plugin can ask for another size at any moment, through the host's
    // request_resize. The window follows it, and then makes sure the result is
    // still somewhere the user can reach.
    if (child != nullptr && child == getContentComponent())
        placeOnScreen();
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
