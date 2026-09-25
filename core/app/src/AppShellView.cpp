#include "AppShellView.h"

#include <utility>

namespace daw::app
{

AppShellView::AppShellView(const ui::Tokens& tokens,
                           std::unique_ptr<ui::TitleBarView> titleBar,
                           std::unique_ptr<juce::Component> content)
    : tokens_(tokens)
    , titleBar_(std::move(titleBar))
    , content_(std::move(content))
{
    addAndMakeVisible(*titleBar_);
    addAndMakeVisible(*content_);
}

void AppShellView::resized()
{
    auto area = getLocalBounds();
    titleBar_->setBounds(area.removeFromTop(tokens_.integer("metric.titleBar.height")));
    content_->setBounds(area);
}

bool AppShellView::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    if (!mods.isCommandDown() || mods.isAltDown())
        return false;

    const auto code = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    auto item = 0;

    if (code == 'n' && !mods.isShiftDown())
        item = ui::TitleBarView::newItem;
    else if (code == 'o' && !mods.isShiftDown())
        item = ui::TitleBarView::openItem;
    else if (code == 's')
        item = mods.isShiftDown() ? ui::TitleBarView::saveAsItem : ui::TitleBarView::saveItem;

    if (item == 0)
        return false;

    titleBar_->runMenuItem(item);
    return true;
}

} // namespace daw::app
