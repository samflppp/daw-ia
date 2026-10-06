#pragma once

#include "daw/domain/live/Router.h"
#include "daw/domain/live/TypingKeyboard.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/LiveHost.h"
#include "daw/ui/model/ProjectObserver.h"
#include "daw/ui/model/Selection.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace daw::app
{

class RawKeyboard;

// Playing live, on the application's side (S23).
//
// Which track the keys play: the one chosen in the rack, the selection's
// track. None chosen, or the chosen one removed: none, and a key plays
// nothing — the screen says so.
//
// The computer's keyboard: read by the place of each key, on a thread of its
// own (RawKeyboard, Raw Input on Windows), so that a key never waits for the
// message thread, nor for a repaint of the canvas. The message thread only
// says what the keyboard may do: whether the mode is on, whether a text field
// has the focus (then the keys type), whether the window is in front (when
// it goes behind, what the keys hold is released).
class LivePlay final : public ui::LiveHost,
                       private juce::ChangeListener,
                       private juce::FocusChangeListener,
                       private juce::Timer
{
public:
    LivePlay(domain::live::Router& router,
             const domain::ProjectState& state,
             ui::Selection& selection,
             ui::ProjectObserver& project);
    ~LivePlay() override;

    LivePlay(const LivePlay&) = delete;
    LivePlay& operator=(const LivePlay&) = delete;
    LivePlay(LivePlay&&) = delete;
    LivePlay& operator=(LivePlay&&) = delete;

    // The track the keys play now, empty when none.
    [[nodiscard]] std::string target() const { return router_.target(); }

    // Reads the selection and the project again. Called on every change of
    // either; a test calls it to act at once.
    void follow();

    // The computer's keyboard, for a verification that presses keys by their
    // scan code without a keyboard: the same entry the keyboard thread uses.
    [[nodiscard]] domain::live::TypingKeyboard& keys() noexcept { return keys_; }

    // --- ui::LiveHost
    [[nodiscard]] bool keyboardPlaying() const override { return keys_.playing(); }
    void setKeyboardPlaying(bool playing) override;
    [[nodiscard]] bool keyboardAvailable() const override;
    [[nodiscard]] int octave() const override { return keys_.octave(); }
    [[nodiscard]] int velocity() const override { return keys_.velocity(); }
    void setVelocity(int velocity) override { keys_.setVelocity(velocity); }
    [[nodiscard]] std::string targetName() const override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void globalFocusChanged(juce::Component* focused) override;
    void timerCallback() override;

    domain::live::Router& router_;
    const domain::ProjectState& state_;
    ui::Selection& selection_;
    ui::ProjectObserver& project_;
    domain::live::TypingKeyboard keys_;
    std::unique_ptr<RawKeyboard> raw_;
};

} // namespace daw::app
