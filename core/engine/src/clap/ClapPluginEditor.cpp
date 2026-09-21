#include "ClapPluginInstance.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace daw::engine::clap_host
{
namespace
{

// The plugin's own window, embedded in a JUCE component.
//
// CLAP asks for a native parent handle, so the plugin is attached to the native
// window this editor lives in, and the editor is sized to what the plugin asks
// for. That is why the application shows a plugin in a window of its own whose
// content is nothing but this editor: any margin would shift the plugin's
// drawing away from where it thinks it is.
class Editor final : public juce::AudioProcessorEditor
{
public:
    explicit Editor(PluginInstance& owner)
        : juce::AudioProcessorEditor(owner)
        , owner_{owner}
    {
        const auto* gui = owner_.gui();
        if (gui == nullptr)
            return;

        // A size before anything else, and always. A failed create() used to
        // leave this component at zero by zero: the window then opened tiny and
        // black, which is what a plugin reopened twice looked like.
        setSize(fallbackWidth, fallbackHeight);

        // Created once per plugin instance, never once per window.
        //
        // Closing the window used to destroy the plugin's GUI and opening it
        // again used to build a new one. CLAP allows it, but a plugin is
        // entitled to come back from a second create() in another state -- and
        // Vital came back drawing smaller than the window it had just asked
        // for, which is the black band around its interface. Hiding and
        // showing the same GUI asks the plugin for nothing it has to redo.
        //
        // The GUI is destroyed with the plugin instance, which is where it has
        // to happen anyway: a GUI outliving its plugin is a crash.
        if (!owner_.isGuiCreated())
        {
            if (!gui->create(owner_.plugin(), PluginInstance::nativeWindowApi(), owner_.isGuiFloating()))
            {
                juce::Logger::writeToLog("plugin editor: " + owner_.getName() +
                                         " refused to create its window");
                return;
            }

            owner_.setGuiCreated(true);

            // The scale of the display, not juce::Desktop::getGlobalScaleFactor().
            // That one is a user setting on JUCE's own zoom and is 1 here; the
            // display is at 1.5. Telling the plugin 1 while Windows draws at
            // 1.5 is what left a black band around Vital: it reported a size
            // in the pixels of one scale and drew in the pixels of another.
            if (gui->set_scale != nullptr)
                gui->set_scale(owner_.plugin(), displayScale());
        }

        created_ = true;

        readSizeFromPlugin();

        if (gui->can_resize != nullptr)
            setResizable(gui->can_resize(owner_.plugin()), false);

        if (owner_.isGuiFloating() && gui->show != nullptr)
        {
            // A plugin that only knows how to float keeps its own window. The
            // editor stays as a marker so closing it closes the plugin window.
            gui->show(owner_.plugin());
        }
    }

    ~Editor() override
    {
        // Hidden, not destroyed: see the constructor. The plugin instance owns
        // the GUI and destroys it when it goes.
        if (const auto* gui = owner_.gui(); gui != nullptr && created_ && gui->hide != nullptr)
            gui->hide(owner_.plugin());
    }

    void parentHierarchyChanged() override
    {
        // The native handle only exists once this component belongs to a real
        // window, which is after construction.
        attachToNativeWindow();
    }

    void resized() override
    {
        const auto* gui = owner_.gui();
        if (gui == nullptr || !created_ || owner_.isGuiFloating() || gui->set_size == nullptr)
            return;

        if (gui->can_resize != nullptr && !gui->can_resize(owner_.plugin()))
            return;

        const auto scale = displayScale();
        gui->set_size(owner_.plugin(),
                      static_cast<std::uint32_t>(std::max(1.0, std::round(getWidth() * scale))),
                      static_cast<std::uint32_t>(std::max(1.0, std::round(getHeight() * scale))));
    }

    void paint(juce::Graphics& g) override
    {
        // Only visible for the fraction of a second before the plugin draws, or
        // for a plugin whose window failed to attach.
        g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
    }

private:
    static constexpr int fallbackWidth = 480;
    static constexpr int fallbackHeight = 320;

    // CLAP speaks in the pixels of the display; a JUCE component is measured
    // before the display scale is applied. The two differ by exactly this
    // factor, and forgetting it is a window that does not fit its plugin.
    [[nodiscard]] static double displayScale()
    {
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        return display != nullptr && display->scale > 0.0 ? display->scale : 1.0;
    }

    void readSizeFromPlugin()
    {
        const auto* gui = owner_.gui();
        if (gui == nullptr || gui->get_size == nullptr)
            return;

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (!gui->get_size(owner_.plugin(), &width, &height) || width == 0 || height == 0)
            return;

        const auto scale = displayScale();
        setSize(static_cast<int>(std::round(static_cast<double>(width) / scale)),
                static_cast<int>(std::round(static_cast<double>(height) / scale)));
    }

    void attachToNativeWindow()
    {
        const auto* gui = owner_.gui();
        if (gui == nullptr || !created_ || attached_ || owner_.isGuiFloating())
            return;

        auto* handle = getWindowHandle();
        if (handle == nullptr)
            return;

        clap_window_t window{};
        window.api = PluginInstance::nativeWindowApi();

#if JUCE_LINUX || JUCE_BSD
        // An X11 window id is an integer, not a pointer: the cast has to go
        // through an integer type or it does not compile at all.
        window.x11 = static_cast<clap_xwnd>(reinterpret_cast<std::uintptr_t>(handle));
#else
        window.ptr = handle;
#endif

        if (gui->set_parent != nullptr && gui->set_parent(owner_.plugin(), &window))
        {
            attached_ = true;

            if (gui->show != nullptr)
                gui->show(owner_.plugin());

            // Asked again, and this is the time that counts: a plugin knows how
            // big it wants to be once it has a parent and has been shown, not
            // at create(). Reading it only before was why a reopened editor
            // came back with a black band around it -- the window kept the
            // first answer and the plugin drew to the second.
            readSizeFromPlugin();
        }
    }

    PluginInstance& owner_;
    bool created_{false};
    bool attached_{false};
};

} // namespace

juce::AudioProcessorEditor* PluginInstance::createEditor()
{
    if (gui_ == nullptr)
        return nullptr;

    return new Editor{*this};
}

} // namespace daw::engine::clap_host
