#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <vector>

namespace daw::ui
{

// Play, stop, the playhead, the tempo, undo and redo. The workspace switch has
// moved to the title bar: it does not act on the music.
//
// Every button goes through the bus, including undo and redo: the panel holds
// no state of its own about the project, and asks the bus what it is allowed to
// offer. A redo button that is enabled because the panel thinks something was
// undone is a button that lies the first time a command arrives from elsewhere
// — from MCP, from a copilot, from a replay.
//
// PAT and SONG choose what play plays: the pattern being edited, alone and
// looping from its start, or the whole arrangement. The two buttons are one
// transient command each, and the lit one is read from the transport state,
// so a copilot that switches mode lights the right button with no code here.
//
// The playhead is the exception, and a deliberate one. It does not come from
// the project: it comes from the clock, sixty times a second, because that is
// where it actually is.
//
// The tempo and the signature are FL's readouts:
//   wheel over the tempo       the project's tempo, one BPM a notch
//   click on the tempo         a menu: type it, or automate it
//   wheel over the signature   the numerator, one a notch
//   click on the signature     type it: "6/8"
// A turn of the wheel is one history entry, however many notches it counted.
// The tempo shown is the project's — the point at the origin — even where the
// tempo is automated further on; the caption says when it is.
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

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;

    // Where the two readouts are drawn. The verification aims at them.
    [[nodiscard]] juce::Rectangle<int> tempoArea() const { return readoutArea(1); }
    [[nodiscard]] juce::Rectangle<int> signatureArea() const { return readoutArea(2); }

    // The menu a click on the tempo opens, in its order.
    enum TempoMenu
    {
        typeTempoItem = 1,
        automateTempoItem = 2
    };

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
    [[nodiscard]] juce::String signatureText() const;

    // 0 the position, 1 the tempo, 2 the signature.
    [[nodiscard]] juce::Rectangle<int> readoutArea(int index) const;

    void showTempoMenu();
    void typeTempo();
    void typeSignature();
    void automateTempo();

    // The gesture a turn of the wheel runs in, opened by its first notch and
    // closed when the wheel has rested long enough.
    [[nodiscard]] domain::ExecuteOptions wheelOptions(const char* label);
    void closeWheelGesture();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    const TransportClock& clock_;
    Selection& selection_;

    class IconButton;

    std::unique_ptr<IconButton> rewind_;
    std::unique_ptr<IconButton> play_;
    std::unique_ptr<IconButton> stop_;
    std::unique_ptr<IconButton> undo_;
    std::unique_ptr<IconButton> redo_;

    juce::TextButton patternMode_{"PAT"};
    juce::TextButton songMode_{"SONG"};

    // What the last refresh drew, so a tick that changes nothing repaints
    // nothing. The readout is redrawn thirty times a second and the rest of the
    // panel almost never.
    juce::String lastPosition_;

    std::optional<domain::GestureId> wheelGesture_;
    juce::uint32 lastWheelMs_{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportPanel)
};

} // namespace daw::ui
