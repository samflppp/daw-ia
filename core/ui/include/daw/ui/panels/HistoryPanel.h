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
class HistoryPanel final : public juce::Component, private juce::ChangeListener
{
public:
    explicit HistoryPanel(const PanelContext& context);
    ~HistoryPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void refresh();

    // The list is drawn newest first, so row 0 is the last entry.
    [[nodiscard]] juce::Rectangle<int> listArea() const;
    [[nodiscard]] int rowAt(juce::Point<int> point) const;
    [[nodiscard]] std::size_t entryAtRow(int row) const;

    // Undoes or redoes until the cursor sits just after the given entry.
    void walkTo(std::size_t entryIndex);

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    History& history_;

    juce::TextButton undo_{"Annuler"};
    juce::TextButton redo_{u8"Rétablir"};
    int hovered_{-1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryPanel)
};

} // namespace daw::ui
