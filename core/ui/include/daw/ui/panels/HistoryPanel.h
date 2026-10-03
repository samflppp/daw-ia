#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstddef>

namespace daw::ui
{

// What has been done, by whom, and how far back the project can go.
//
// The list is the history log's, the moves go to the bus. Clicking an entry
// walks the bus to it, one undo or one redo at a time: there is no "jump to
// state" on the bus and there must not be one, because every step of the walk
// is an operation observers and the journal are told about.
class HistoryPanel final : public juce::Component, public juce::TooltipClient, private juce::ChangeListener
{
public:
    explicit HistoryPanel(const PanelContext& context);
    ~HistoryPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // How far the list is scrolled, in pixels: what the verification reads.
    [[nodiscard]] int scrolled() const noexcept { return scroll_; }
    // How many entries the list has heard of: behind the log until its next message.
    [[nodiscard]] std::size_t entryCount() const noexcept { return entriesSeen_; }

    // Over a line the copilot wrote with a context — the mix by the AI — the
    // sentences of what it did, each with the measure it cites (S20).
    [[nodiscard]] juce::String getTooltip() override;
    [[nodiscard]] juce::String sentencesAtRow(int row) const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refresh();

    // The list is drawn newest first, so row 0 is the last entry.
    [[nodiscard]] juce::Rectangle<int> listArea() const;
    [[nodiscard]] int contentHeight() const;
    void scrollTo(int offset);
    [[nodiscard]] int rowAt(juce::Point<int> point) const;
    [[nodiscard]] std::size_t entryAtRow(int row) const;

    // Undoes or redoes until the cursor sits just after the given entry.
    void walkTo(std::size_t entryIndex);

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    History& history_;
    MixHost& mix_;

    juce::TextButton undo_{"Annuler"};
    juce::TextButton redo_{u8"Rétablir"};
    int hovered_{-1};

    // The list scrolled (S21). Newest on top: at the top it shows what is
    // added; scrolled down by the hand, it keeps the entries it showed.
    int scroll_{0};
    int dragFrom_{0};
    bool middleDragging_{false};
    std::size_t entriesSeen_{0};

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryPanel)
};

} // namespace daw::ui
