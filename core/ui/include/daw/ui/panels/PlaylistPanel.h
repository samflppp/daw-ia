#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <string>
#include <vector>

namespace daw::ui
{

// The playlist: the arrangement, one lane per pattern, then one lane per track
// that holds audio clips.
//
// A pattern lane is a pattern and never a track: a placement carries no track,
// because the pattern already says on which tracks it sounds. An audio lane is
// a track, because an audio clip does carry one. Both kinds of lane are
// derived from the state at paint time; nothing about the screen enters the
// domain, and a copilot that lays a pattern sees it appear with no code here.
//
// The gestures are FL Studio's:
//   click in a pattern lane    pattern.place, at the bar under the pointer
//   drop a sample              a new track and an audio.place, at the bar
//   drag a block               the whole selection moves, one history entry
//   right-click a block        removes it, or the whole selection
//   Ctrl + drag on empty       selects every block the rectangle touches
//   Ctrl + click               adds a block to the selection, or takes it out
//                              (Ctrl + Shift + click, the S10 gesture, too)
//   Ctrl+C, Ctrl+V             copies the selection, pastes it at the playhead
//   Ctrl+B                     duplicates the selection right after itself
//   Delete                     removes the selection
// Placing and moving snap to the bar; Shift snaps to the beat.
//
// The view, which is this screen's and never the project's:
//   wheel                      scrolls the lanes
//   Shift + wheel              scrolls the timeline (a trackpad's sideways
//                              swipe does too)
//   Ctrl + wheel               zooms the timeline around the pointer
//   the two scroll bars        what they always do
// At rest the whole song fits the width, as it did before there was a zoom —
// but never narrower than a readable bar: past that, the timeline scrolls.
// Zooming out stops there. In song mode, while playing, the view turns the
// page when the playhead leaves it.
//
// There is no resize. A pattern's length belongs to the pattern, and an audio
// clip lasts as long as its sample.
class PlaylistPanel final : public juce::Component,
                            public juce::DragAndDropTarget,
                            public juce::FileDragAndDropTarget,
                            private juce::ChangeListener,
                            private juce::ScrollBar::Listener,
                            private juce::Timer
{
public:
    explicit PlaylistPanel(const PanelContext& context);
    ~PlaylistPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // A sample from the browser, or a file from the system.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // One block of the playlist: a placement of a pattern, or an audio clip.
    struct Item
    {
        bool audio{false};
        std::string id;

        friend bool operator==(const Item& lhs, const Item& rhs)
        {
            return lhs.audio == rhs.audio && lhs.id == rhs.id;
        }
    };

    // What is selected, in the order it was selected. Read by the scripted
    // verification, which checks what a Ctrl + drag caught.
    [[nodiscard]] const std::vector<Item>& selected() const noexcept { return selected_; }

    // Where a beat of a lane is drawn now, scrolled and zoomed as the view is,
    // and the area blocks are drawn in. The verification aims its clicks with
    // them, and scrolls with the wheel when a target is out of sight.
    [[nodiscard]] juce::Point<int> pointFor(int lane, double beats) const;
    [[nodiscard]] juce::Rectangle<int> timelineArea() const { return gridArea(); }

    // The view: pixels per beat, and the first beat on the left.
    [[nodiscard]] double beatWidth() const;
    [[nodiscard]] double firstBeat() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void timerCallback() override;

    // --- lanes
    [[nodiscard]] int patternLaneCount() const;
    [[nodiscard]] std::vector<domain::TrackId> audioTracks() const;
    [[nodiscard]] int laneCount() const;
    [[nodiscard]] int laneOfTrack(domain::TrackId trackId) const;

    // --- geometry
    [[nodiscard]] juce::Rectangle<int> headerArea() const; // the lane names
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;
    [[nodiscard]] juce::Rectangle<int> gridArea() const;

    // How long the timeline is: the arrangement and four bars of room after
    // it, never fewer than sixteen bars. Read at paint time, so the timeline
    // grows as the song does.
    [[nodiscard]] double timelineBeats() const;

    // The width at which the whole timeline fits, never under the readable
    // floor. The narrowest a beat can be drawn, and the width at rest.
    [[nodiscard]] double fitBeatWidth() const;
    [[nodiscard]] double viewBeats() const; // how many beats the grid shows
    [[nodiscard]] int lanesHeight() const;
    [[nodiscard]] int firstLanePixel() const;

    // Moves the view, clamped to the timeline, and the scroll bars with it.
    void setView(double firstBeat, std::optional<double> zoom);
    void setFirstLanePixel(int pixel);
    void updateScrollBars();
    void followPlayhead();
    [[nodiscard]] double beatAtX(int x) const;
    [[nodiscard]] int xForBeat(double beats) const;
    [[nodiscard]] int laneAtY(int y) const; // -1 outside any lane
    [[nodiscard]] static double snap(double beats, bool fine);

    // --- items
    [[nodiscard]] std::vector<Item> items() const;
    [[nodiscard]] std::optional<double> startOf(const Item& item) const;
    [[nodiscard]] double lengthOf(const Item& item) const;
    [[nodiscard]] int laneOf(const Item& item) const;
    [[nodiscard]] juce::Rectangle<int> bounds(const Item& item, double offsetBeats = 0.0) const;
    [[nodiscard]] std::optional<Item> itemAt(juce::Point<int> point) const;
    [[nodiscard]] bool isSelected(const Item& item) const;

    // --- painting
    void paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintLanes(juce::Graphics& g, juce::Rectangle<int> grid, juce::Rectangle<int> headers) const;
    void paintBlocks(juce::Graphics& g, juce::Rectangle<int> grid) const;
    void paintPlayhead(juce::Graphics& g) const;
    void paintEmpty(juce::Graphics& g) const;

    // The playhead's column, in song mode only: pattern mode plays a pattern
    // from its own start, which is nowhere on this timeline.
    [[nodiscard]] std::optional<int> playheadX() const;

    // --- editing
    void placeAt(int lane, double beats);
    void showLaneMenu(int lane);
    void renamePattern(domain::PatternId patternId);
    void dropSample(const juce::File& file, juce::Point<int> at);

    void moveSelection(double offsetBeats);
    void removeSelection();
    void copySelection();
    void pasteAt(double beats);
    void duplicateSelection();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    const TransportClock& clock_;
    SampleHost& samples_;

    std::vector<Item> selected_;

    // A move in progress: the selection is drawn shifted, and one group of
    // moves leaves when the mouse is released — one history entry however
    // many blocks moved and however long the hand hesitated.
    struct Move
    {
        double grabBeats{0.0};
        double offsetBeats{0.0};
    };
    std::optional<Move> move_;

    // A Ctrl + drag in progress, in panel coordinates.
    std::optional<juce::Rectangle<int>> band_;
    juce::Point<int> bandStart_;

    // What Ctrl+C took: each block with its start relative to the first one.
    struct Copied
    {
        bool audio{false};
        domain::PatternId patternId{};
        domain::TrackId trackId{};
        std::optional<domain::SampleRef> sample;
        double offsetBeats{0.0};
    };
    std::vector<Copied> clipboard_;

    std::optional<int> paintedPlayheadX_;

    // The view. No zoom means "fit": the width follows the song.
    std::optional<double> zoom_;
    double firstBeat_{0.0};
    int firstLanePixel_{0};
    juce::ScrollBar horizontal_{false};
    juce::ScrollBar vertical_{true};

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistPanel)
};

} // namespace daw::ui
