#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

namespace daw::ui
{

// The panel a manifest asks for and this build does not have yet.
//
// It names itself and says nothing else. It is not a mockup of the panel to
// come: a convincing drawing of a feature that does not exist is the one thing
// a demonstration must never contain.
class PlaceholderPanel final : public juce::Component
{
public:
    explicit PlaceholderPanel(const PanelContext& context);

    void paint(juce::Graphics& g) override;

private:
    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    std::string id_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderPanel)
};

} // namespace daw::ui
