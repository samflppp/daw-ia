#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui
{

// The mixer: one strip per channel, then one per bus, then the master, pinned
// on the right.
//
// A strip is what the domain calls a strip: a fader, a pan, a mute, a solo, its
// inserts, where it goes and what it sends. Every control goes through the bus,
// with a gesture around a drag, so a fader sweep is one Ctrl+Z; nothing here
// holds a value the project does not. The meters beside each fader read the
// engine's taps, thirty times a second.
//
// A fader or a pan with an automation line follows the line while the song
// plays, and the project stopped. The hand wins while it holds the slider;
// released, the slider goes back to the curve. Moving it still writes the
// project's value, which the line covers as long as it has points.
//
// "Mixer par l'IA" calls no model. It runs the check of MixingReadiness against
// what the copilot can reach, and says what is still missing.
class MixerPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit MixerPanel(const PanelContext& context);
    ~MixerPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // The last report of the readiness check, as it is shown. Read by the
    // verification.
    [[nodiscard]] juce::String readinessReport() const { return report_.getText(); }

    // The strips on screen, channels and buses then the master: for the
    // verification, which clicks their controls.
    [[nodiscard]] std::vector<juce::Component*> strips() const;

private:
    class Strip;
    class Meter;
    class Content;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void rebuild();
    void refresh();
    void runReadiness();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    const TransportClock& clock_;
    CopilotHost& copilot_;
    LevelSource& levels_;
    bool titled_{false};

    juce::TextButton addBus_;
    juce::TextButton readiness_;
    juce::Viewport viewport_;
    std::unique_ptr<Content> content_;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::unique_ptr<Strip> master_;
    juce::TextEditor report_;

    std::vector<domain::TrackId> shownIds_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanel)
};

} // namespace daw::ui
