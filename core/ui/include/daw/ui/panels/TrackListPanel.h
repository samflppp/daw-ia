#pragma once

#include "daw/ui/FrameTicker.h"
#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui
{

// One row per track: name, volume, mute, bypass of the chain.
//
// The rows are rebuilt from ProjectState whenever the bus says something
// changed. They hold no copy of the project: a row's fader shows the volume the
// state holds, and moving it sends a command instead of writing a value
// anywhere. That is what makes a track created by MCP, by a replay or by a
// copilot appear here without a line of code for each case.
class TrackListPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit TrackListPanel(const PanelContext& context);
    ~TrackListPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Row;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void frame();
    void rebuild();
    void addTrack();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    const TransportClock& clock_;

    juce::Viewport viewport_;
    std::unique_ptr<juce::Component> rowHolder_;
    std::vector<Row*> rows_;
    juce::TextButton add_{"+  Nouvelle piste"};

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    // Last, so the first to go: no image is asked of a panel being taken
    // apart. One call per image of the screen (S18 bis).
    FrameTicker frames_{*this, [this] { frame(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackListPanel)
};

} // namespace daw::ui
