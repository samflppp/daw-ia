#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace daw::ui
{

// The playlist: the arrangement, one lane per pattern.
//
// A lane is a pattern and never a track. That is what the pattern model
// imposes: a placement carries no track, because the pattern already says on
// which tracks it sounds, so a lane per track would show the same laying on
// several lanes and drag them all at once. One lane per pattern shows each
// laying once, and moving it moves exactly what it says.
//
// The lanes are derived, not stored: lane n is the n-th pattern of the
// project. Nothing about the screen enters the domain, and a copilot that lays
// a pattern sees it appear on its lane with no code here.
//
// Every edit leaves as a command:
//   click in a lane           pattern.place, at the bar under the pointer
//   drag a block              placement.move, one gesture, one entry
//   right-click a block       placement.remove
//   lane header, right-click  pattern.rename, pattern.remove
// Placing snaps to the bar; Shift snaps to the beat.
//
// There is no resize. The length belongs to the pattern, and stretching one
// laying out of eight must not stretch the seven others.
class PlaylistPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PlaylistPanel(const PanelContext& context);
    ~PlaylistPanel() override;

    void paint(juce::Graphics& g) override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    // --- geometry
    [[nodiscard]] juce::Rectangle<int> headerArea() const; // the lane names
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    [[nodiscard]] juce::Rectangle<int> gridArea() const;

    // How many beats the grid shows: the arrangement and four bars of room
    // after it, never fewer than sixteen bars. Read at paint time, so the
    // timeline grows as the song does.
    [[nodiscard]] double visibleBeats() const;
    [[nodiscard]] double beatAtX(int x) const;
    [[nodiscard]] int xForBeat(double beats) const;
    [[nodiscard]] int laneAtY(int y) const; // -1 outside any lane
    [[nodiscard]] juce::Rectangle<int> blockBounds(const domain::Placement& placement) const;

    // The placement drawn under that point, or null. The last one drawn wins,
    // which is the one on top.
    [[nodiscard]] const domain::Placement* placementAt(juce::Point<int> point) const;

    [[nodiscard]] static double snap(double beats, bool fine);

    void paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintLanes(juce::Graphics& g, juce::Rectangle<int> grid, juce::Rectangle<int> headers) const;
    void paintBlocks(juce::Graphics& g, juce::Rectangle<int> grid) const;
    void paintPlayhead(juce::Graphics& g) const;
    void paintEmpty(juce::Graphics& g) const;

    // The playhead's column, in song mode only: pattern mode plays a pattern
    // from its own start, which is nowhere on this timeline.
    [[nodiscard]] std::optional<int> playheadX() const;

    void placeAt(int lane, double beats);
    void showLaneMenu(int lane);
    void renamePattern(domain::PatternId patternId);

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    const TransportClock& clock_;

    struct Drag
    {
        domain::PlacementId placementId{};
        domain::GestureId gesture{};

        // Where in the block the pointer took it, in beats, so the block does
        // not jump to put its start under the pointer.
        double grabBeats{0.0};
        double lastStartBeats{0.0};
    };

    std::optional<Drag> drag_;
    std::optional<int> paintedPlayheadX_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistPanel)
};

} // namespace daw::ui
