#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

namespace daw::ui
{

// The channel rack: one row per track, a grid of steps, and the pattern the
// whole beatmaker is working on.
//
// A lit cell is a note. Not a step in a parallel structure, not a flag compiled
// into notes later — a note, in the row that track plays in the current
// pattern, at the channel's own pitch and one step long. The piano roll shows
// the same rows as rectangles, and the two agree because there is nothing to
// agree about: they read the same vector.
//
// The panel owns nothing. It draws what ProjectState says at paint time and
// every edit leaves as a command, so a grid filled by the copilot appears here
// with no code of its own.
//
// The grid follows the pattern's length: sixteen steps of a sixteenth fill four
// beats, and a pattern of eight beats shows thirty-two of them. The pattern is
// the truth and the number of cells is a reading of it — never the reverse.
class ChannelRackPanel final : public juce::Component,
                               private juce::ChangeListener,
                               private juce::Timer,
                               private juce::AsyncUpdater
{
public:
    explicit ChannelRackPanel(const PanelContext& context);
    ~ChannelRackPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void handleAsyncUpdate() override;

    // Pattern mode plays the pattern this rack shows, and nothing else.
    void followCurrentPattern();

    [[nodiscard]] const domain::Pattern* pattern() const;

    // How many steps the grid shows, and how long one is. Both read from the
    // pattern: the resolution chooses how finely it is cut, never how long it
    // is.
    [[nodiscard]] double stepBeats() const;
    [[nodiscard]] int stepCount() const;

    // --- geometry
    [[nodiscard]] juce::Rectangle<int> gridArea() const;
    [[nodiscard]] juce::Rectangle<int> channelArea() const;
    [[nodiscard]] int rowAtY(int y) const;  // -1 outside any row
    [[nodiscard]] int stepAtX(int x) const; // -1 outside the grid
    [[nodiscard]] juce::Rectangle<int> cellBounds(int row, int step) const;

    // The note that occupies a cell, or null. A cell holds at most one: two
    // notes of the same channel on the same step would be one sound and two
    // things to click.
    [[nodiscard]] const domain::Note* noteAt(int row, int step) const;

    void paintChannels(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintEmpty(juce::Graphics& g) const;

    // --- editing
    //
    // A click decides what the whole drag does, and the drag repeats it: a
    // stroke that started by lighting a cell lights every cell it crosses, and
    // one that started by clearing one clears them. A drag that flipped each
    // cell it touched would erase what it had just drawn on the way back.
    enum class Stroke
    {
        light,
        clear
    };

    void applyStrokeAt(int row, int step);
    void lightCell(int row, int step);
    void clearCell(int row, int step);

    // Where the playhead is in the grid, in steps, or nothing when the
    // transport is outside the placement this panel follows.
    [[nodiscard]] std::optional<int> playheadStep() const;

    void createPattern();
    void rebuildPatternChooser();
    void editChannelPitch(int row);

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    const TransportClock& clock_;

    // How finely the pattern is cut on screen. A screen setting and nothing
    // else: changing it writes no command and moves no note.
    double resolutionBeats_{0.25};

    struct Drag
    {
        Stroke stroke{Stroke::light};
        domain::GestureId gesture{};

        // The last cell the stroke acted on, so crossing one twice does not
        // write twice.
        int row{-1};
        int step{-1};
    };

    std::optional<Drag> drag_;

    std::optional<int> paintedPlayheadStep_;

    juce::ComboBox patternChooser_;
    juce::ComboBox resolutionChooser_;
    juce::TextButton addPattern_{"+ Pattern"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelRackPanel)
};

} // namespace daw::ui
