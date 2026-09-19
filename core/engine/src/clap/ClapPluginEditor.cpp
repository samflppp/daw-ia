#include "ClapPluginInstance.h"

#include <juce_gui_basics/juce_gui_basics.h>

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

        created_ = gui->create(owner_.plugin(), PluginInstance::nativeWindowApi(), owner_.isGuiFloating());
        if (!created_)
            return;

        owner_.setGuiCreated(true);

        if (gui->set_scale != nullptr)
            gui->set_scale(owner_.plugin(), juce::Desktop::getInstance().getGlobalScaleFactor());

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (gui->get_size != nullptr && gui->get_size(owner_.plugin(), &width, &height) && width > 0 &&
            height > 0)
            setSize(static_cast<int>(width), static_cast<int>(height));
        else
            setSize(fallbackWidth, fallbackHeight);

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
        if (const auto* gui = owner_.gui(); gui != nullptr && created_)
        {
            if (gui->hide != nullptr)
                gui->hide(owner_.plugin());
            if (gui->destroy != nullptr)
                gui->destroy(owner_.plugin());

            owner_.setGuiCreated(false);
        }
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

        gui->set_size(owner_.plugin(),
                      static_cast<std::uint32_t>(std::max(1, getWidth())),
                      static_cast<std::uint32_t>(std::max(1, getHeight())));
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
