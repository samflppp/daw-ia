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

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    // The clip being edited: the first of the selected track. Null when no
    // track is selected, or when the track holds no clip yet.
    [[nodiscard]] const domain::Clip* clip() const;
    [[nodiscard]] const domain::Track* track() const;

    // --- geometry. The one place pixels and music meet.
    [[nodiscard]] juce::Rectangle<int> gridArea() const;
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

    // A drag in progress: what it does to the note, the grab offset in beats,
    // and the gesture that makes the whole movement one history entry.
    //
    // Moving and stretching are two modes and two commands, never one: an undo
    // has to give back either the position or the length, not a note that had
    // both at once and that the user never saw.
    enum class DragMode
    {
        move,
        resize
    };

    struct Drag
    {
        domain::NoteId noteId{};
        DragMode mode{DragMode::move};
        double grabOffsetBeats{0.0};
        int grabPitch{0};
        domain::GestureId gesture{};
        bool moved{false};
    };

    std::optional<Drag> drag_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollPanel)
};

} // namespace daw::ui
