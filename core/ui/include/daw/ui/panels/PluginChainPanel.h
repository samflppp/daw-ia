#pragma once

#include "daw/ui/MiddleDragScroll.h"
#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui
{

// The plugin chain of the selected track: one slot per instance, in order.
//
// A slot opens the plugin's own editor, contours it, or takes it out. None of
// the three is done here: opening goes to the host, the other two go to the
// bus. The panel holds no plugin and no window.
class PluginChainPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit PluginChainPanel(const PanelContext& context);
    ~PluginChainPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Slot;

    // Menu identifiers that cannot collide with the index of an installed
    // plugin, whatever the machine holds.
    static constexpr int rescanItemId = 100000;
    static constexpr int noneItemId = 100001;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void rebuild();
    void refreshChrome();
    void showInsertMenu();
    void insert(const domain::PluginRef& ref);

    [[nodiscard]] const domain::Track* track() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    PluginHost& plugins_;

    juce::Viewport viewport_;
    MiddleDragScroll middleDrag_{viewport_};
    std::unique_ptr<juce::Component> slotHolder_;
    std::vector<Slot*> slots_;
    juce::TextButton add_{"+  Plugin"};

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginChainPanel)
};

} // namespace daw::ui
