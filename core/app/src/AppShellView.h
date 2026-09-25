#pragma once

#include "daw/ui/TitleBarView.h"
#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace daw::app
{

// What the main window shows: the title bar across the top, and under it the
// workspace or the gallery. It is a layout host and nothing more, apart from
// the four File shortcuts, which it hears because every key the workspace
// does not use climbs up to it.
class AppShellView final : public juce::Component
{
public:
    AppShellView(const ui::Tokens& tokens,
                 std::unique_ptr<ui::TitleBarView> titleBar,
                 std::unique_ptr<juce::Component> content);

    [[nodiscard]] ui::TitleBarView& titleBar() noexcept { return *titleBar_; }

    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    const ui::Tokens& tokens_;
    std::unique_ptr<ui::TitleBarView> titleBar_;
    std::unique_ptr<juce::Component> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppShellView)
};

} // namespace daw::app
