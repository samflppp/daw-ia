#pragma once

#include "daw/domain/generation/Constraints.h"
#include "daw/ui/FrameTicker.h"
#include "daw/ui/PanelRegistry.h"
#include "daw/ui/model/GhostProposal.h"
#include "daw/ui/model/PromptReader.h"
#include "daw/ui/model/TransformProposal.h"
#include "daw/ui/panels/GenerationPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
// not the timeline. The whole pattern fits the width until the wheel over the
// ruler zooms in; the middle button held down drags the view on both axes.
// Neither is an edit, and neither leaves a history entry. Which pattern is on screen is not chosen here — the
// transport chooses it, this panel follows, and both read the same Selection.
// Two choosers for one choice would be the second truth the S7bis review
// refused.
//
// Which channel it writes for is chosen in its header, as in FL: a menu of the
// rack's channels. That one is not a second chooser: it writes the same
// Selection a click in the rack writes, so the two always agree.
//
// Which note is selected is not project state and stays here: selecting is not
// an edit, and undoing a move must not undo a click.
//
// Under the notes, the velocity lane: one stem per note, drawn over by hand,
// as in FL's event editor. Its gestures are described in
// PianoRollVelocityLane.cpp.
//
// Generation (S16, PianoRollGeneration.cpp). The zone first: Shift + drag on
// the ruler picks a range, a Ctrl + drag band picks notes, and with neither the
// zone is the whole pattern. Then the prompt: the "Générer" button of the
// header, the chip at the end of a range, or Ctrl+G, open the generation
// window docked under the panel -- never over the notes. Nothing is proposed
// until a prompt is written; Enter reads it, the proposal is drawn in grey
// with a short sentence under it, Alt + wheel or the arrows walk through the
// variants, Tab or Valider writes them as one group from the generator, Escape
// drops them. Écouter (Ctrl+Space) loops the grey notes through the track's
// instrument, written nowhere (ListeningHost). Until Tab nothing is written:
// the proposal is a GhostProposal held by this panel.
class PianoRollPanel final : public juce::Component, private juce::ChangeListener
{
public:
    // One sixteenth. The grid the beatmaker workspace draws is the grid it
    // snaps to: a note that lands between two lines it can see is a note the
    // user has to fight.
    static constexpr double gridStepBeats = 0.25;

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

    // The channel menu of the header. The verification chooses with it.
    [[nodiscard]] juce::ComboBox& channelChooser() noexcept { return channelChooser_; }

    // Where a click lands on that beat and that pitch.
    [[nodiscard]] juce::Point<int> pointFor(double beats, int pitch) const;

    // The horizontal view: pixels per beat, and the first beat on the left.
    // The ruler is where the wheel zooms. The verification reads them.
    [[nodiscard]] double beatWidth() const;
    [[nodiscard]] double firstBeat() const;
    [[nodiscard]] juce::Rectangle<int> ruler() const { return rulerArea(); }
    [[nodiscard]] int topPitch() const noexcept { return topPitch_; }

    // The velocity lane, and where a note's stem would reach at a velocity.
    [[nodiscard]] juce::Rectangle<int> velocityLane() const { return velocityArea(); }
    [[nodiscard]] juce::Point<int> velocityPointFor(const domain::Note& note, int velocity) const;

    // --- generation. Read by the verification.
    [[nodiscard]] bool proposing() const noexcept { return proposal_.has_value() || rework_.has_value(); }

    // Notes of the zone reworked rather than new ones (S16): open when the
    // zone held notes at the prompt.
    [[nodiscard]] const TransformProposal* rework() const noexcept
    {
        return rework_.has_value() ? &*rework_ : nullptr;
    }
    [[nodiscard]] const GhostProposal* proposal() const noexcept
    {
        return proposal_.has_value() ? &*proposal_ : nullptr;
    }
    [[nodiscard]] const std::vector<domain::generation::GhostNote>& ghostNotes() const noexcept
    {
        return ghosts_;
    }
    [[nodiscard]] juce::TextEditor& promptField() noexcept { return bar_.field(); }
    [[nodiscard]] GenerationPanel& generationBar() noexcept { return bar_; }
    [[nodiscard]] bool generationOpen() const noexcept { return bar_.isVisible(); }
    [[nodiscard]] juce::TextButton& generateButton() noexcept { return generate_; }
    [[nodiscard]] bool listeningToProposal() const noexcept { return listeningHere_; }

    // The sentence under the grey notes, in a musician's words.
    [[nodiscard]] juce::String proposalSentence() const;
    [[nodiscard]] std::optional<std::pair<double, double>> range() const { return range_; }
    // The technical line, shown folded under "Détails".
    [[nodiscard]] juce::String proposalLine() const;
    [[nodiscard]] double lastGenerationMs() const noexcept;

    // How long building the style took at the last Ctrl+G: the project
    // counted, mixed with the others and the base. Logged and verified.
    [[nodiscard]] double lastStyleMs() const noexcept { return lastStyleMs_; }
    [[nodiscard]] int variantRank() const noexcept
    {
        return proposal_.has_value() ? proposal_->rank() : (rework_.has_value() ? rework_->rank() : -1);
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void frame();

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

    // --- the velocity lane (PianoRollVelocityLane.cpp)
    [[nodiscard]] juce::Rectangle<int> velocityArea() const;
    [[nodiscard]] int yForVelocity(int velocity) const;
    [[nodiscard]] int velocityAtY(int y) const;
    [[nodiscard]] bool isVelocityEditable(domain::NoteId id) const;
    [[nodiscard]] int shownVelocity(const domain::Note& note) const;
    void paintVelocityLane(juce::Graphics& g) const;
    void strokeVelocity(juce::Point<int> from, juce::Point<int> to);
    void commitVelocityStroke();

    // A stroke being drawn over the stems: the velocity each note crossed
    // will get, shown now and sent on release.
    std::optional<std::vector<std::pair<domain::NoteId, int>>> velocityStroke_;
    juce::Point<int> strokeLast_;

    // --- generation (PianoRollGeneration.cpp)

    [[nodiscard]] bool generationKey(const juce::KeyPress& key);
    void openPrompt();
    void generateFromPrompt();
    void showVariant(int delta);
    void acceptProposal();
    void closeProposal();
    void refreshProposal();
    void placeBar();

    // ▶ Écouter: the proposal on screen, looped, written nowhere.
    void toggleListening();
    void listenAgain(); // another variant, or regenerated: heard at once
    void stopListening();
    void showProposal(const PromptReader::Reading& reading);
    [[nodiscard]] std::optional<std::pair<double, double>> shownRange() const;

    // The zone Ctrl+G generates into: the range on the ruler, else the span of
    // the picked notes, else the whole pattern.
    [[nodiscard]] std::pair<double, double> zone() const;
    [[nodiscard]] bool zoneHasNotes() const;

    // The chip at the end of the range on the ruler, which opens the window.
    [[nodiscard]] std::optional<juce::Rectangle<int>> rangeChip() const;
    void paintRange(juce::Graphics& g, juce::Rectangle<int> area) const;
    void paintGhosts(juce::Graphics& g, juce::Rectangle<int> area) const;

    // Everything under the header that is not the generation window.
    [[nodiscard]] juce::Rectangle<int> bodyArea() const;

    GenerationPanel bar_;
    juce::TextButton generate_;
    PromptReader& reader_;
    ListeningHost& listening_;
    bool listeningHere_{false}; // this panel started what is heard
    std::optional<GhostProposal> proposal_;
    std::optional<TransformProposal> rework_;
    std::vector<domain::generation::GhostNote> ghosts_;

    // What either proposal says about where it writes.
    struct Target
    {
        domain::TrackId track;
        domain::PatternId pattern;
        double fromBeats{0.0};
        double toBeats{0.0};
    };
    [[nodiscard]] std::optional<Target> target() const;
    void reshow(); // the notes and the window after a change of variant or context
    juce::String promptedText_;
    juce::String styleLine_;
    double styleShare_{0.0};
    PromptReader::Reading lastReading_; // what the proposal on screen was read from
    double lastStyleMs_{0.0};

    // The range picked on the ruler, in pattern beats, and where the drag
    // that picks it started.
    std::optional<std::pair<double, double>> range_;
    std::optional<double> rangeAnchor_;

    void addNoteAt(juce::Point<int> point);
    void removeNote(domain::NoteId noteId);

    // The channel chooser of the header, FL's: which channel of the rack
    // this piano roll writes for. The same Selection as a click in the rack.
    void rebuildChannelChooser();

    // What opening the chosen track's row takes: the commands, and the
    // pattern and row they make. Empty when the row is open already. A first
    // note drawn in a closed row sends these and the note as one group.
    struct RowOpening
    {
        std::vector<std::unique_ptr<domain::Command>> commands;
        domain::PatternId patternId{};
        domain::ClipId clipId{};
    };
    [[nodiscard]] RowOpening openRow() const;
    void selectOpened(const RowOpening& opening);

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

    // The horizontal view. No zoom is the width that fits the pattern, and
    // a zoom out past it goes back to that state.
    double firstBeat_{0.0};
    std::optional<double> zoom_;
    [[nodiscard]] double fitBeatWidth() const;
    void setView(double first, std::optional<double> zoom);

    // A drag of the middle button: where it started, and the view then.
    struct Pan
    {
        juce::Point<int> start;
        double firstBeat{0.0};
        int topPitch{0};
    };
    std::optional<Pan> pan_;

    // Moves the window onto the notes of the row when none of them is in
    // sight: on a change of row, and when the panel is resized.
    void revealNotes();
    std::string revealedRow_; // the track and the row last revealed

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

        // Where the note was last painted, on the grid and in the velocity
        // lane: a step of the drag repaints that and where the note is now,
        // not the panel (S18 bis).
        std::pair<juce::Rectangle<int>, juce::Rectangle<int>> painted;
    };

    // The note being dragged, as painted: its rectangle with the width of a
    // selection outline, and its stem's column in the velocity lane.
    [[nodiscard]] std::pair<juce::Rectangle<int>, juce::Rectangle<int>> dragArea() const;

    std::optional<Drag> drag_;

    juce::ComboBox channelChooser_;

    // True in a page window, whose title bar names the panel already.
    bool titled_{false};

    // Last, so the first to go: no image is asked of a panel being taken
    // apart. One call per image of the screen (S18 bis).
    FrameTicker frames_{*this, [this] { frame(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollPanel)
};

} // namespace daw::ui
