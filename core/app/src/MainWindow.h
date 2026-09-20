#pragma once

#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace daw::app
{

class MainWindow final : public juce::DocumentWindow
{
public:
    // The window owns what it shows and knows nothing else about it: the
    // application decides whether that is the workspace or the style gallery.
    MainWindow(const juce::String& title, const ui::Tokens& tokens, std::unique_ptr<juce::Component> content);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace daw::app
