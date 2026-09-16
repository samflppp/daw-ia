#pragma once

#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::app
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& title, const ui::Tokens& tokens);

    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace daw::app
