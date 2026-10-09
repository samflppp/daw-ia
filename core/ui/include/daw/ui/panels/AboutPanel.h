#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui
{

// « À propos » (S26): which build this is, the mentions the licences require,
// and the whole text of the third-party licences, to read and copy.
class AboutPanel final : public juce::Component
{
public:
    explicit AboutPanel(const PanelContext& context);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // What the page shows, for a verification to read.
    [[nodiscard]] juce::String heading() const;
    [[nodiscard]] juce::String mentions() const;
    [[nodiscard]] juce::String licences() const { return licences_.getText(); }

private:
    [[nodiscard]] juce::Rectangle<int> textArea() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    AboutHost& about_;
    bool titled_;
    juce::TextEditor licences_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutPanel)
};

} // namespace daw::ui
