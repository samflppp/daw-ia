#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace daw::app::timing
{

// How the verifications time a repaint (S18 bis, shared with the canvas in
// S19): a median, a 95th percentile, the worst.
struct Timing
{
    double median{0.0};
    double p95{0.0};
    double worst{0.0};
    int count{0};
};

[[nodiscard]] Timing of(std::vector<double> ms);

// One real repaint of a component and everything in it, the way its window's
// peer paints it: at the display's scale, clipped to `area`, into an image of
// the renderer asked for. NativeImageType is Direct2D on Windows in JUCE 8,
// the window's own renderer; SoftwareImageType is the one the light mode would
// use. What is timed is the painting up to the context being flushed, not the
// allocation of the image.
[[nodiscard]] double
paintMs(juce::Component& component, juce::Rectangle<int> area, const juce::ImageType& type);

// `samples` whole repaints after `warmUps` thrown away: the first paints of a
// surface fill the glyph and gradient caches, and they are not what a person
// feels on the hundredth. `between`, when given, runs before each one, the
// warm-ups included, and is not timed: a step of a pan.
[[nodiscard]] Timing measure(juce::Component& component,
                             const juce::ImageType& type,
                             int warmUps,
                             int samples,
                             const std::function<void(int)>& between = {});

[[nodiscard]] std::string describe(const Timing& timing);

} // namespace daw::app::timing
