#pragma once

#include "daw/ui/DawLookAndFeel.h"
#include "daw/ui/Tokens.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui
{

// Every token and every control, on one screen, behind --gallery.
//
// It exists because the three hygiene rules cannot see ugliness. A build that
// compiles, passes its tests and violates no rule can still be illegible, and
// the only way to find out is to look at it. This is the surface that is
// looked at: change a token, run the gallery, see what moved.
//
// It is a development tool and it stays one. No panel depends on it, and it
// reads nothing from the project.
class GalleryView final : public juce::Component
{
public:
    GalleryView(const Tokens& tokens, DawLookAndFeel& lookAndFeel);
    ~GalleryView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Swatch
    {
        juce::String path;
        juce::String label;
    };

    void paintSection(juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& title);
    void paintSwatches(juce::Graphics& g, juce::Rectangle<int>& area, const std::vector<Swatch>& swatches);
    void paintTypeScale(juce::Graphics& g, juce::Rectangle<int>& area);

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;

    juce::TextButton play_{"Lecture"};
    juce::TextButton stop_{"Arret"};
    juce::TextButton record_{"Enregistrer"};
    juce::TextButton disabled_{"Retablir"};
    juce::ToggleButton mute_{"M"};
    juce::ToggleButton bypass_{"B"};
    juce::Slider volume_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};
    juce::Slider quiet_{juce::Slider::LinearHorizontal, juce::Slider::NoTextBox};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GalleryView)
};

} // namespace daw::ui
