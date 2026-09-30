#include "DisplayMode.h"

#include "daw/ui/FrameTicker.h"

namespace daw::app::display
{
namespace
{

constexpr const char* settingKey = "display.light";

// JUCE names its Windows renderers; it does not number them in a promise.
constexpr const char* softwareRenderer = "Software Renderer";
constexpr const char* direct2dRenderer = "Direct2D";

} // namespace

bool isLight(const juce::PropertySet* settings)
{
    return settings != nullptr && settings->getBoolValue(settingKey, false);
}

void setLight(juce::PropertySet* settings, bool light)
{
    if (settings != nullptr)
        settings->setValue(settingKey, light);
}

void apply(bool light)
{
    ui::FrameTicker::setPace(light ? ui::FrameTicker::Pace::light : ui::FrameTicker::Pace::fluid);

    for (int index = 0; index < juce::ComponentPeer::getNumPeers(); ++index)
    {
        if (auto* peer = juce::ComponentPeer::getPeer(index); peer != nullptr)
            applyTo(*peer);
    }

    juce::Logger::writeToLog(juce::String("display: ") + (light ? "light" : "fluid"));
}

void applyTo(juce::ComponentPeer& peer)
{
    const auto light = ui::FrameTicker::pace() == ui::FrameTicker::Pace::light;
    const auto wanted =
        peer.getAvailableRenderingEngines().indexOf(light ? softwareRenderer : direct2dRenderer);

    // Another system names its one renderer otherwise: nothing to choose.
    if (wanted >= 0 && wanted != peer.getCurrentRenderingEngine())
    {
        peer.setCurrentRenderingEngine(wanted);
        peer.getComponent().repaint();
    }
}

juce::String rendererOf(juce::ComponentPeer& peer)
{
    return peer.getAvailableRenderingEngines()[peer.getCurrentRenderingEngine()];
}

} // namespace daw::app::display
