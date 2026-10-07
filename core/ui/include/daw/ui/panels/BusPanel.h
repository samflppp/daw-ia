#pragma once

#include "daw/ui/PanelRegistry.h"
#include "daw/ui/model/BusHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace daw::ui
{

// The smart buses window (S24): the effects the project carries several
// times, each proposal said in a sentence; one tried on a copy, its before
// and after measured and listened to at equal loudness — and seen in the
// flux —, kept in one group or refused.
class BusPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit BusPanel(const PanelContext& context);
    ~BusPanel() override;

    BusPanel(const BusPanel&) = delete;
    BusPanel& operator=(const BusPanel&) = delete;
    BusPanel(BusPanel&&) = delete;
    BusPanel& operator=(BusPanel&&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // What the window shows, as text, for the verification.
    [[nodiscard]] std::vector<std::string> shown() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refresh();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    BusHost& buses_;
    bool titled_{false};

    juce::TextButton search_;
    juce::ComboBox choice_;
    juce::TextButton tryOut_;
    juce::TextButton before_;
    juce::TextButton after_;
    juce::TextButton stop_;
    juce::TextButton keep_;
    juce::TextButton refuse_;
    juce::Label status_;
};

} // namespace daw::ui
