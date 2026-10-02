#include "VerificationTiming.h"

#include <algorithm>
#include <cmath>

namespace daw::app::timing
{

Timing of(std::vector<double> ms)
{
    Timing timing{};
    if (ms.empty())
        return timing;

    std::sort(ms.begin(), ms.end());
    const auto n = ms.size();
    timing.count = static_cast<int>(n);
    timing.median = ms[n / 2];
    timing.p95 = ms[std::min(n - 1, static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n))) - 1)];
    timing.worst = ms.back();
    return timing;
}

double paintMs(juce::Component& component, juce::Rectangle<int> area, const juce::ImageType& type)
{
    const auto scale = juce::Component::getApproximateScaleFactorForComponent(&component);
    juce::Image image{juce::Image::ARGB,
                      std::max(1, juce::roundToInt(static_cast<float>(area.getWidth()) * scale)),
                      std::max(1, juce::roundToInt(static_cast<float>(area.getHeight()) * scale)),
                      false,
                      type};

    const auto started = juce::Time::getMillisecondCounterHiRes();
    {
        juce::Graphics g{image};
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setOrigin(-area.getPosition());
        g.reduceClipRegion(area);
        component.paintEntireComponent(g, true);
    }
    return juce::Time::getMillisecondCounterHiRes() - started;
}

Timing measure(juce::Component& component,
               const juce::ImageType& type,
               int warmUps,
               int samples,
               const std::function<void(int)>& between)
{
    for (int index = 0; index < warmUps; ++index)
    {
        if (between)
            between(index);
        static_cast<void>(paintMs(component, component.getLocalBounds(), type));
    }

    std::vector<double> ms;
    for (int index = 0; index < samples; ++index)
    {
        if (between)
            between(warmUps + index);
        ms.push_back(paintMs(component, component.getLocalBounds(), type));
    }
    return of(std::move(ms));
}

std::string describe(const Timing& timing)
{
    return "médiane " + juce::String(timing.median, 2).toStdString() + " ms, 95e centile " +
           juce::String(timing.p95, 2).toStdString() + " ms, pire " +
           juce::String(timing.worst, 2).toStdString() + " ms (" + std::to_string(timing.count) +
           " repeints)";
}

} // namespace daw::app::timing
