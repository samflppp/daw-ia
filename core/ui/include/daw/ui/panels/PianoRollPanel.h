#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace daw::ui
{

// The notes the selected track plays in the current pattern, and the four ways
// to change them: click to add, drag to move, Delete or right-click to remove.
//
// The panel owns no note. Every pixel it draws is read from ProjectState at
// paint time, and every edit leaves as a command. That is what makes the drag
// undoable in one step and the whole row reappear after a reload without this
// file knowing either fact.
//
// It shows one row of one pattern: the horizontal axis is the pattern's length,
// not the timeline. Which pattern is on screen is not chosen here — the channel
// rack chooses it, this panel follows, and both read the same Selection. Two
// choosers for one choice would be the second truth the S7bis review refused.
//
// Which note is selected is not project state and stays here: selecting is not
// an edit, and undoing a move must not undo a click.
class PianoRollPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PianoRollPanel(const PanelContext& context);
    ~PianoRollPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    // The notes picked by clicks, Ctrl + click and Ctrl + drag: what Ctrl+C,
    // Ctrl+B and Delete act on. Read by the verification.
    [[nodiscard]] const std::vector<domain::NoteId>& picked() const noexcept { return picked_; }

    // Where a note is drawn: the verification aims its clicks with it.
    [[nodiscard]] juce::Rectangle<int> noteBounds(const domain::Note& note) const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    // The row being edited: what the selected track plays in the current
    // pattern. Null when no track is selected, when the project holds no
    // pattern, or when that track has no row in it yet.
    [[nodiscard]] const domain::Clip* clip() const;
    [[nodiscard]] const domain::Track* track() const;
    [[nodiscard]] const domain::Pattern* pattern() const;

    // The pattern's own length, and where one of its beats is on the
    // transport. The row carries neither: a row is content, and both of these
    // are position.
    [[nodiscard]] double patternLength() const;
    [[nodiscard]] double transportBeat(double patternBeats) const;

    // --- geometry. The one place pixels and music meet.
    [[nodiscard]] juce::Rectangle<int> gridArea() const;

    // The bar numbers, above the grid. Clicking there moves the playhead, so
    // it is a rectangle this panel has to be able to name.
    [[nodiscard]] juce::Rectangle<int> rulerArea() const;

    // Where the playhead sits on this panel's axis, or nothing when the
    // playhead is outside the clip being edited. The axis is the span of that
    // clip, so a clip that does not start at bar one needs its own offset
    // taken off -- without it the playhead of a second clip was drawn at the
    // wrong place, and dragging it would have written the wrong beat.
    [[nodiscard]] std::optional<int> playheadX() const;

    void movePlayheadTo(int x);
    [[nodiscard]] int rowsVisible() const;
    [[nodiscard]] int yForPitch(int pitch) const;
    [[nodiscard]] int pitchAtY(int y) const;
    [[nodiscard]] double beatAtX(int x) const;
    [[nodiscard]] int xForBeat(double beats) const;
    [[nodiscard]] double quantise(double beats) const;
    [[nodiscard]] const domain::Note* noteAt(juce::Point<int> point) const;

    // True on the last few pixels of a note: there the drag stretches it
    // instead of moving it. The grip is not drawn — the cursor says it, and a
    // handle on a note one sixteenth wide would be the whole note.
    [[nodiscard]] bool isOnResizeGrip(const domain::Note& note, juce::Point<int> point) const;

    void paintKeyboard(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintRuler(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintGrid(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintNotes(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintPlayhead(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintEmpty(juce::Graphics& g) const;

    void addNoteAt(juce::Point<int> point);
    void removeNote(domain::NoteId noteId);

    // Opens this track's row in the current pattern, creating the pattern too
    // when the project holds none. One group, therefore one Ctrl+Z.
    void addRow();

    // Makes pattern mode play the pattern on screen. Called when the user
    // picks a pattern, which is the only moment what pattern mode plays
    // changes.
    void followCurrentPattern();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ProjectObserver& project_;
    Selection& selection_;
    const TransportClock& clock_;

    // The top of the pitch window, moved by the wheel. The window itself is as
    // tall as the panel allows: a piano roll that scrolls when it does not need
    // to is a piano roll that hides notes for nothing.
    int topPitch_{84};

    domain::NoteId selectedNote_{};

    // Every picked note, the one being dragged among them; a band drawn with
    // Ctrl, in panel coordinates.
    std::vector<domain::NoteId> picked_;
    std::optional<juce::Rectangle<int>> band_;
    juce::Point<int> bandStart_;
    Clipboard& clipboard_;

    [[nodiscard]] bool isPicked(domain::NoteId id) const;
    void copyPicked();
    void pasteNotes(bool duplicate);
    void removePicked();

    // Where the playhead was last painted. Without it the timer invalidated
    // the column the playhead is moving to and never the one it is leaving, so
    // every frame left a line behind and the panel filled up with them.
    std::optional<int> paintedPlayheadX_;

    // True while the playhead is being dragged along the ruler.
    bool draggingPlayhead_{false};

    // A drag in progress: what it does to the note, the grab offset in beats,
    // and the gesture that makes the whole movement one history entry.
    //
    // Moving and stretching are two modes and two commands, never one: an undo
    // has to give back either the position or the length, not a note that had
    // both at once and that the user never saw.
    enum class DragMode
    {
        move,
        resize,
        velocity
    };

    struct Drag
    {
        domain::NoteId noteId{};
        DragMode mode{DragMode::move};
        double grabOffsetBeats{0.0};
        int grabPitch{0};

        // Where the velocity drag started, in pixels and in velocity. A
        // velocity is dragged relative to what it was, so a note at 20 and a
        // note at 120 both follow the hand instead of jumping to it.
        int grabY{0};
        int grabVelocity{0};
        domain::GestureId gesture{};
        bool moved{false};
    };

    std::optional<Drag> drag_;

    juce::TextButton addRow_{"+ Ligne"};

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollPanel)
};

} // namespace daw::ui
