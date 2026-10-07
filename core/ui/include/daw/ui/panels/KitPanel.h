#pragma once

#include "daw/ui/PanelRegistry.h"
#include "daw/ui/model/KitHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace daw::ui
{

// The kit window (S24): measure the samples of the browser's folders, set
// three axes — where the direction puts them by default —, compose, listen
// to two bars of it or to one element, lay it down. Each element says why it
// was chosen, with the numbers it rests on; what is missing is said.
class KitPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit KitPanel(const PanelContext& context);
    ~KitPanel() override;

    KitPanel(const KitPanel&) = delete;
    KitPanel& operator=(const KitPanel&) = delete;
    KitPanel(KitPanel&&) = delete;
    KitPanel& operator=(KitPanel&&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;

    // --- the gestures, as the buttons and the list make them
    void measure();
    void compose();
    void listen();
    void listenTo(std::size_t pick);
    bool pose();
    void setAxes(const domain::kit::Axes& axes);
    [[nodiscard]] domain::kit::Axes axes() const;

    // What the window shows, as text, for the verification.
    [[nodiscard]] std::vector<std::string> shown() const;
    [[nodiscard]] juce::String statusLine() const { return status_.getText(); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refresh();
    [[nodiscard]] juce::Rectangle<int> listArea() const;

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    KitHost& kit_;
    bool titled_{false};
    bool axesFromDirection_{true};

    juce::TextButton measure_;
    juce::TextButton compose_;
    juce::TextButton listen_;
    juce::TextButton stop_;
    juce::TextButton pose_;
    juce::TextButton direction_;
    juce::Label brightLabel_;
    juce::Label ampleLabel_;
    juce::Label dirtyLabel_;
    juce::Slider bright_;
    juce::Slider ample_;
    juce::Slider dirty_;
    juce::Label status_;
};

} // namespace daw::ui
