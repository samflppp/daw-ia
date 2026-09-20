#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui
{

// Play, stop, the playhead, the tempo, undo and redo, and the workspace switch.
//
// Every button goes through the bus, including undo and redo: the panel holds
// no state of its own about the project, and asks the bus what it is allowed to
// offer. A redo button that is enabled because the panel thinks something was
// undone is a button that lies the first time a command arrives from elsewhere
// — from MCP, from a copilot, from a replay.
//
// The playhead is the exception, and a deliberate one. It does not come from
// the project: it comes from the clock, sixty times a second, because that is
// where it actually is.
class TransportPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    // What a transport button draws. Glyphs would have been cheaper and wrong:
    // the shapes a musician recognises are not characters in a font, and a
    // square typed as brackets reads as a mistake before it reads as a stop.
    enum class Icon
    {
        rewind,
        play,
        stop,
        undo,
        redo
    };

    explicit TransportPanel(const PanelContext& context);
    ~TransportPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void refresh();
    void paintReadout(juce::Graphics& g,
                      juce::Rectangle<int> area,
                      const juce::String& value,
                      const juce::String& label,
                      bool strong) const;

    [[nodiscard]] juce::String positionText() const;
    [[nodiscard]] juce::String tempoText() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    const TransportClock& clock_;
    WorkspaceHost& workspaces_;

    class IconButton;

    std::unique_ptr<IconButton> rewind_;
    std::unique_ptr<IconButton> play_;
    std::unique_ptr<IconButton> stop_;
    std::unique_ptr<IconButton> undo_;
    std::unique_ptr<IconButton> redo_;

    std::vector<std::unique_ptr<juce::TextButton>> workspaceButtons_;

    // What the last refresh drew, so a tick that changes nothing repaints
    // nothing. The readout is redrawn thirty times a second and the rest of the
    // panel almost never.
    juce::String lastPosition_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};

} // namespace daw::ui
