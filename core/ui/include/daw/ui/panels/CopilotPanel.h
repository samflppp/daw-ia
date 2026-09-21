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
class CopilotPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit CopilotPanel(const PanelContext& context);
    ~CopilotPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refresh();
    void sendRequest();

    [[nodiscard]] juce::Rectangle<int> transcriptArea() const;
    [[nodiscard]] juce::String statusText() const;
    [[nodiscard]] juce::Colour statusColour() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    CopilotHost& copilot_;

    juce::TextEditor request_;
    juce::TextButton send_{"Envoyer"};
    juce::TextButton restart_{"Relancer"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CopilotPanel)
};

} // namespace daw::ui
