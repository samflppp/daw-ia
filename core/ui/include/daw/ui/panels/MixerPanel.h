#pragma once

#include "daw/ui/FrameTicker.h"
#include "daw/ui/MiddleDragScroll.h"
#include "daw/ui/PanelRegistry.h"
#include "daw/ui/panels/MixProposalView.h"

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
// « Mixer » runs the mix by the AI (S20) through MixHost: while it measures,
// decides and verifies, and while its proposal waits for the person, the
// proposal takes the place of the strips; the same button cancels a run, and
// keeping or refusing puts the strips back.
class InsertSlots;

class MixerPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit MixerPanel(const PanelContext& context);
    ~MixerPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // The proposal of the mix by the AI, as it is shown: for the verification.
    [[nodiscard]] const MixProposalView& proposal() const { return *proposal_; }
    [[nodiscard]] MixProposalView& proposal() { return *proposal_; }

    // The strips on screen, channels and buses then the master: for the
    // verification, which clicks their controls.
    [[nodiscard]] std::vector<juce::Component*> strips() const;

    // The effects of a strip as its slots show them, and their gestures (S24):
    // for the verification. Null for a strip not on screen.
    [[nodiscard]] InsertSlots* insertsOf(domain::TrackId strip) const;

private:
    class Strip;
    class Meter;
    class Content;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void frame();
    void rebuild();
    void refresh();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    const TransportClock& clock_;
    MixHost& mix_;
    PluginHost& plugins_;
    LevelSource& levels_;
    bool titled_{false};

    juce::TextButton addBus_;
    juce::TextButton mixButton_;
    juce::Label mixStatus_;
    juce::Viewport viewport_;
    MiddleDragScroll middleDrag_{viewport_};
    std::unique_ptr<Content> content_;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::unique_ptr<Strip> master_;
    std::unique_ptr<MixProposalView> proposal_;

    std::vector<domain::TrackId> shownIds_;

    // Last, so the first to go: no image is asked of a panel being taken
    // apart. One call per image of the screen (S18 bis).
    FrameTicker frames_{*this, [this] { frame(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerPanel)
};

} // namespace daw::ui
