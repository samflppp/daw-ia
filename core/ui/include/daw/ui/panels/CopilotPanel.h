#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui
{

// Where the user talks to the copilot: a field, a send button, the
// conversation, and what the copilot is doing.
//
// The panel knows nothing of the process, the socket or the model. It reads a
// CopilotHost and calls ask(), exactly as the chain panel reads a PluginHost
// and the transport calls the bus. That is also what keeps the interface
// alive: ask() returns at once, and the answer arrives later as a change
// message.
//
// The push-to-talk (S25) lives here too, read from a VoiceHost: a button held
// with the mouse (the right Ctrl does the same), what the microphone hears
// while it is held, and the phrase understood put in the field — its
// uncertain words marked — where it is corrected before it leaves. A sure
// phrase shows the time left before it goes; a doubtful one says why, and
// waits for Entrée.
class CopilotPanel final : public juce::Component,
                           private juce::ChangeListener,
                           private juce::TextEditor::Listener,
                           private juce::Timer
{
public:
    explicit CopilotPanel(const PanelContext& context);
    ~CopilotPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // For a check: the field, and the button held.
    [[nodiscard]] juce::TextEditor& field() noexcept { return request_; }
    [[nodiscard]] juce::Component& talkButton() noexcept { return talk_; }

private:
    // The button that is held: press on the way down, release on the way up.
    class HoldButton final : public juce::TextButton
    {
    public:
        HoldButton();
        std::function<void()> onPress;
        std::function<void()> onRelease;
        std::function<void()> onTap; // when holding means nothing: install

    private:
        void mouseDown(const juce::MouseEvent& event) override;
        void mouseUp(const juce::MouseEvent& event) override;
    };

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void textEditorTextChanged(juce::TextEditor& editor) override;
    void textEditorEscapeKeyPressed(juce::TextEditor& editor) override;
    void timerCallback() override;
    void refresh();
    void sendRequest();
    void showPhrase();

    [[nodiscard]] juce::Rectangle<int> transcriptArea() const;
    [[nodiscard]] juce::Rectangle<int> statusArea() const;
    [[nodiscard]] juce::String statusText() const;
    [[nodiscard]] juce::Colour statusColour() const;
    [[nodiscard]] bool voiceSpeaks() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    CopilotHost& copilot_;
    VoiceHost& voice_;

    juce::TextEditor request_;
    juce::TextButton send_{"Envoyer"};
    juce::TextButton restart_{"Relancer"};
    HoldButton talk_;
    juce::TextButton confirm_{"Confirmer"};
    juce::TextButton refuse_{"Annuler"};

    // The phrase heard, as put in the field: it is not the person's typing,
    // and a change to it holds a sure phrase.
    VoiceHost::Stage shownStage_{VoiceHost::Stage::idle};
    juce::String placed_;

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CopilotPanel)
};

} // namespace daw::ui
