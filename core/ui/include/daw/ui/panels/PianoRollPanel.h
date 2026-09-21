#pragma once

#include "daw/ui/PanelRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace daw::ui
{

// The notes of the selected track's clip, and the four ways to change them:
// click to add, drag to move, Delete or right-click to remove.
//
// The panel owns no note. Every pixel it draws is read from ProjectState at
// paint time, and every edit leaves as a command. That is what makes the drag
// undoable in one step and the whole clip reappear after a reload without this
// file knowing either fact.
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

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    // The clip being edited: the selected one, or the first of the track. Null
    // when no track is selected, or when the track holds no clip yet.
    //
    // One clip at a time is what this panel is: its horizontal axis is the
    // span of that clip, not the timeline. Showing several at once would be an
    // arrangement view, which is a different panel. What was wrong until now
    // is that the other clips were unreachable, not that they were off screen:
    // the chooser in the header names every clip the track holds.
    [[nodiscard]] const domain::Clip* clip() const;
    [[nodiscard]] const domain::Track* track() const;

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

    // Fills the chooser from the track, and marks the edited clip. Called on
    // every project change, so a clip created by a replay or by a copilot
    // appears here with no code of its own.
    void rebuildClipChooser();
    void addClip();

    // Plays the clip on screen over and over. Called when the user picks or
    // creates a clip, which is the only moment the pattern changes.
    void loopOverEditedClip();

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

    juce::ComboBox clipChooser_;
    juce::TextButton addClip_{"+ Clip"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollPanel)
};

} // namespace daw::ui
