#pragma once

#include "daw/ui/PanelRegistry.h"
#include "daw/ui/model/PatternPreviews.h"
#include "daw/ui/model/PromptReader.h"
#include "daw/ui/model/ZoneProposal.h"
#include "daw/ui/panels/GenerationPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace daw::ui
{

// The playlist: the arrangement, on free lines the way FL Studio has them,
// then one lane per automation line.
// A line is the project's (domain::Lane): the user names it, orders it and
// files any block on it — a placement or an audio clip. It sounds of nothing:
// what a block plays is its pattern's tracks or its clip's track, whatever
// line it is on. Under the last line, one more, empty and unnamed: dropping or
// laying something there creates the line in the same history entry.
//
// The gestures are FL Studio's:
//   click on a line            lays a pattern there, at the bar: the one the
//                              line was made for, else the one being edited
//   drop a sample              a new track and an audio.place, on the line
//                              under the pointer, at the bar
//   drag a block               the whole selection moves, sideways and from
//                              line to line, one history entry
//   right-click a block        removes it, or the whole selection
//   Ctrl + drag on empty       selects every block the rectangle touches
//   Ctrl + click               adds a block to the selection, or takes it out
//                              (Ctrl + Shift + click, the S10 gesture, too)
//   Ctrl+C, Ctrl+V             copies the selection, pastes it at the playhead,
//                              each block on the line it was copied from
//   Ctrl+B                     duplicates the selection right after itself,
//                              on the same lines
//   Delete                     removes the selection
//   drag a line's name         moves the line, its blocks with it
//   double-click a line's name renames it; right-click: rename, insert,
//                              remove, and the pattern the line was made for
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
//
// Under the ruler, as soon as the tempo changes past the origin, the tempo
// lane: FL's tempo automation, drawn as the step line it is. Its gestures are
// described in PlaylistTempoLane.cpp.
//
// Then one lane per automation line, strip by strip: the same grammar as the
// tempo lane, described in PlaylistAutomationLane.cpp.
//
// Alt + drag over several lines draws a zone of generation (S17): one prompt,
// a part per line in the role the line's name or content says, heard before it
// is written and written as one history entry. Described in PlaylistZone.cpp.
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

    // The view: pixels per beat, and the first beat on the left. The wheel
    // zooms over the ruler, or with Ctrl anywhere; the middle button held
    // down drags the view on both axes. The ruler is read by the verification.
    [[nodiscard]] juce::Rectangle<int> ruler() const { return rulerArea(); }
    [[nodiscard]] int firstLane() const { return firstLanePixel(); }
    [[nodiscard]] double beatWidth() const;
    [[nodiscard]] double firstBeat() const;

    // How many pattern previews were built since the panel was made: one per
    // pattern and per change of its notes, never per repaint and never per
    // placement. Read by the verification.
    [[nodiscard]] std::size_t previewBuilds() const noexcept { return previews_.builds(); }

    // The tempo lane, empty while the tempo is not automated, and where a
    // tempo change at that beat and that tempo is drawn. The verification
    // aims with them.
    [[nodiscard]] juce::Rectangle<int> tempoLane() const { return tempoLaneArea(); }
    [[nodiscard]] juce::Point<int> tempoPointFor(double beats, double bpm) const;

    // The lane of an automation line, and where a value at a beat is drawn
    // in it. Nothing for a line the playlist does not show. The verification
    // aims with them.
    [[nodiscard]] std::optional<int> laneOfAutomation(domain::AutomationLineId line) const;
    [[nodiscard]] std::optional<juce::Point<int>>
    automationPointFor(domain::AutomationLineId line, double beats, double value) const;

    // The line the last request to see one named: its lane is lit.
    [[nodiscard]] domain::AutomationLineId shownAutomation() const noexcept { return shownAutomation_; }

    // --- the zone of generation (PlaylistZone.cpp), read by the verification
    [[nodiscard]] bool hasZone() const noexcept { return zone_.has_value(); }
    [[nodiscard]] const ZoneProposal* zoneProposal() const noexcept
    {
        return zoneProposal_.has_value() ? &*zoneProposal_ : nullptr;
    }
    [[nodiscard]] GenerationPanel& generationBar() noexcept { return bar_; }

    // Opens the window on the zone drawn; Ctrl+G does the same.
    void openZonePrompt();

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void timerCallback() override;

    // --- lanes
    //
    // The lines of the project first, in their order; then the empty one that
    // makes a new line; then, strip by strip, the automation lanes. A lane
    // below freeLaneCount() is a line of the project.
    struct Lane
    {
        enum class Kind
        {
            line,
            fresh,
            automation
        };

        Kind kind{Kind::line};
        domain::LaneId id{};
        domain::TrackId track{};
        domain::AutomationLineId line{};
    };
    [[nodiscard]] std::vector<Lane> lanes() const;
    [[nodiscard]] int freeLaneCount() const;
    [[nodiscard]] int laneCount() const;

    // What a line is called on screen: its name, or, unnamed, what it was
    // made for — its pattern or its track, as S16 labelled it — or its rank.
    [[nodiscard]] juce::String laneLabel(const domain::Lane& lane, int rank) const;

    // --- geometry
    [[nodiscard]] juce::Rectangle<int> bodyArea() const;   // above the generation window
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
    [[nodiscard]] double snap(double beats, bool fine) const;

    // A bar of the project's signature, in beats: 4 in 4/4, 3 in 6/8.
    [[nodiscard]] double barBeats() const;

    // --- the tempo lane (PlaylistTempoLane.cpp)
    struct TempoRange
    {
        double low{0.0};
        double high{0.0};
    };
    [[nodiscard]] int tempoLaneHeight() const; // 0 while not automated
    [[nodiscard]] juce::Rectangle<int> tempoLaneArea() const;
    [[nodiscard]] juce::Rectangle<int> tempoHeaderArea() const;
    [[nodiscard]] TempoRange tempoRange() const;
    [[nodiscard]] int yForTempo(double bpm, TempoRange range) const;
    [[nodiscard]] double tempoAtY(int y, TempoRange range) const;
    [[nodiscard]] std::optional<domain::TempoPointId> tempoPointAt(juce::Point<int> point) const;
    void paintTempoLane(juce::Graphics& g) const;

    // Each answers true when the event was the lane's.
    bool tempoMouseDown(const juce::MouseEvent& event);
    bool tempoMouseDrag(const juce::MouseEvent& event);
    bool tempoMouseUp();
    bool tempoDoubleClick(juce::Point<int> point);
    bool tempoWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel);
    void closeTempoWheel(bool onlyWhenRested = false);

    // --- the automation lanes (PlaylistAutomationLane.cpp)
    [[nodiscard]] juce::Rectangle<int> laneArea(int lane) const;
    [[nodiscard]] const domain::AutomationLine* automationLineIn(int lane) const;
    [[nodiscard]] int
    yForValue(juce::Rectangle<int> area, const domain::AutomationTarget& target, double value) const;
    [[nodiscard]] double
    valueAtY(juce::Rectangle<int> area, const domain::AutomationTarget& target, int y) const;
    [[nodiscard]] std::optional<domain::AutomationPointId> automationPointAt(int lane,
                                                                             juce::Point<int> point) const;
    void paintAutomation(juce::Graphics& g, juce::Rectangle<int> grid) const;
    void showAutomationMenu(domain::AutomationLineId line);
    void revealAutomation(domain::AutomationLineId line);

    bool automationMouseDown(const juce::MouseEvent& event);
    bool automationMouseDrag(const juce::MouseEvent& event);
    bool automationMouseUp();
    bool automationDoubleClick(juce::Point<int> point);
    bool automationWheel(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel);
    void closeAutomationWheel(bool onlyWhenRested = false);

    // --- items
    [[nodiscard]] std::vector<Item> items() const;
    [[nodiscard]] std::optional<double> startOf(const Item& item) const;
    [[nodiscard]] double lengthOf(const Item& item) const;
    [[nodiscard]] int laneOf(const Item& item) const;
    [[nodiscard]] juce::Rectangle<int>
    bounds(const Item& item, double offsetBeats = 0.0, int laneOffset = 0) const;
    [[nodiscard]] std::optional<Item> itemAt(juce::Point<int> point) const;
    [[nodiscard]] bool isSelected(const Item& item) const;

    // --- painting
    void paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintLanes(juce::Graphics& g, juce::Rectangle<int> grid, juce::Rectangle<int> headers) const;
    void paintBlocks(juce::Graphics& g, juce::Rectangle<int> grid) const;

    // What a block shows of its content, under its name: the notes of its
    // pattern, or the waveform of its sample. Nothing under the width below
    // which a picture would be a smear.
    void paintPreview(juce::Graphics& g, const PatternPreview& preview, juce::Rectangle<int> area) const;
    void paintWaveform(juce::Graphics& g,
                       const WaveformPeaks& peaks,
                       juce::Rectangle<int> block,
                       juce::Rectangle<int> area,
                       juce::Rectangle<int> visible) const;
    void paintPlayhead(juce::Graphics& g) const;
    void paintEmpty(juce::Graphics& g) const;

    // The playhead's column, in song mode only: pattern mode plays a pattern
    // from its own start, which is nowhere on this timeline.
    [[nodiscard]] std::optional<int> playheadX() const;

    // --- editing
    void placeAt(int lane, double beats);
    void showLaneMenu(int lane);
    void renamePattern(domain::PatternId patternId);
    void renameLane(domain::LaneId laneId);
    void dropSample(const juce::File& file, juce::Point<int> at);

    // The line a lane index names, for a command: an existing line, or, on
    // the fresh lane, a new one whose lane.create goes first in `commands`.
    // Nothing outside the lines.
    [[nodiscard]] std::optional<domain::LaneId>
    lineFor(int lane, std::vector<std::unique_ptr<domain::Command>>& commands) const;

    void moveSelection(double offsetBeats, int laneOffset);

    // --- the zone of generation (PlaylistZone.cpp)
    struct Zone
    {
        int firstLane{0}; // lines of the project, both included
        int lastLane{0};
        double fromBeats{0.0};
        double toBeats{0.0};
    };
    [[nodiscard]] std::optional<Zone> zoneBetween(juce::Point<int> from, juce::Point<int> to) const;
    [[nodiscard]] juce::Rectangle<int> zoneArea(const Zone& zone) const;
    [[nodiscard]] juce::Rectangle<int> zonePill() const; // « ✦ Générer », at the zone's corner
    void paintZone(juce::Graphics& g, juce::Rectangle<int> grid) const;
    bool zoneKey(const juce::KeyPress& key);
    void generateZone();
    void showZoneProposal(const PromptReader::Reading& reading);
    void showZoneVariant(int delta);
    void acceptZone();
    void closeZone();
    void toggleZoneListening();
    void listenZoneAgain();
    void stopZoneListening();
    void placeBar();
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

    // A tempo point being dragged, in one gesture. Its tempo follows the hand
    // relative to where it was grabbed, so a drag is not stopped by the edge
    // of the lane's range.
    struct TempoDrag
    {
        enum class Axis
        {
            none,
            tempo,
            position
        };

        domain::TempoPointId pointId{};
        juce::Point<int> grab;
        double grabBpm{0.0};
        double bpmPerPixel{1.0};
        Axis axis{Axis::none};
        domain::GestureId gesture{};
    };
    std::optional<TempoDrag> tempoDrag_;
    std::optional<domain::GestureId> tempoWheelGesture_;
    juce::uint32 lastTempoWheelMs_{0};
    bool tempoLaneShown_{false};

    // A point being dragged, on both axes at once, in one gesture.
    struct AutomationDrag
    {
        domain::AutomationLineId line{};
        domain::AutomationPointId point{};
        domain::GestureId gesture{};
    };
    std::optional<AutomationDrag> automationDrag_;
    std::optional<domain::GestureId> automationWheelGesture_;
    juce::uint32 lastAutomationWheelMs_{0};
    domain::AutomationLineId shownAutomation_{};
    std::size_t automationRequests_{0};

    // A move in progress: the selection is drawn shifted, and one group of
    // moves leaves when the mouse is released — one history entry however
    // many blocks moved and however long the hand hesitated.
    struct Move
    {
        double grabBeats{0.0};
        int grabLane{0};
        double offsetBeats{0.0};
        int laneOffset{0};
    };
    std::optional<Move> move_;

    // A line's name being dragged up or down: one lane.move per line crossed,
    // merged by the gesture into one history entry.
    struct LaneDrag
    {
        domain::LaneId id{};
        domain::GestureId gesture{};
        bool moved{false};
    };
    std::optional<LaneDrag> laneDrag_;

    // A Ctrl + drag in progress, in panel coordinates.
    std::optional<juce::Rectangle<int>> band_;
    juce::Point<int> bandStart_;

    // What Ctrl+C took: each block with its start relative to the first one.
    struct Copied
    {
        bool audio{false};
        domain::PatternId patternId{};
        domain::TrackId trackId{};
        domain::LaneId laneId{};
        std::optional<domain::SampleRef> sample;
        double offsetBeats{0.0};
    };
    std::vector<Copied> clipboard_;

    std::optional<int> paintedPlayheadX_;

    // Built when the project changes, read when painting.
    PatternPreviews previews_;

    // The view. No zoom means "fit": the width follows the song.
    std::optional<double> zoom_;

    // A drag of the middle button: where it started, and the view then.
    struct Pan
    {
        juce::Point<int> start;
        double firstBeat{0.0};
        int firstLanePixel{0};
    };
    std::optional<Pan> pan_;
    double firstBeat_{0.0};
    int firstLanePixel_{0};
    juce::ScrollBar horizontal_{false};
    juce::ScrollBar vertical_{true};

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    // The zone of generation, being drawn or drawn, and what was proposed.
    PromptReader& reader_;
    ListeningHost& listening_;
    GenerationPanel bar_;
    std::optional<Zone> zone_;
    std::optional<juce::Point<int>> zoneStart_;
    std::optional<ZoneProposal> zoneProposal_;
    PromptReader::Reading lastReading_;
    juce::String promptedText_;
    bool listeningHere_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistPanel)
};

} // namespace daw::ui
