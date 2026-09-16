#pragma once

#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui
{

// Top-level layout host.
//
// Hygiene rule 2: panels never set their own bounds. RootView (and future
// layout hosts driven by workspace manifests) is the only place that
// assigns positions and sizes, in resized().
class RootView final : public juce::Component
{
public:
    explicit RootView(const Tokens& tokens);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    const Tokens& tokens_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RootView)
};

} // namespace daw::ui
