#pragma once

#include "LevelMonitor.h"
#include "PlaybackProbe.h"
#include "SongExporter.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/TitleBarView.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/WorkspaceView.h"
#include "daw/ui/model/CopilotHost.h"
#include "daw/ui/model/History.h"
#include "daw/ui/model/SampleHost.h"
#include "daw/ui/model/Selection.h"
#include "daw/ui/model/TransportClock.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <string>
#include <vector>

namespace daw::app
{

// The checks a person would run on the real binary, run by the binary itself.
//
// Launched with --verify <folder>, the application builds its window as usual
// and then plays the verification list through the same doors a person uses:
// mouse events sent to the channel rack and the playlist, clicks on the PAT,
// SONG and "+ Pattern" buttons of the transport, Ctrl+Z through the view, a sentence typed to
// the real copilot. It never reaches into a panel to call a private method.
//
// What it cannot do is listen through the speakers. It renders the Edit
// offline instead, after every step that should change the sound, and counts
// where the notes start: the same measurement the engine tests make, on the
// project the window shows. Each step leaves a snapshot of the window and, when
// it rendered, the WAV it listened to — so a person can look and listen after
// the fact, in the order of the list.
//
// Three runs, one per command-line flag:
//   --verify         the list, on an empty project, ending with the state kept
//   --verify-reopen  the same project reopened by a new process: same state,
//                    same history depth
//   --verify-legacy  a project of the first nine weeks: it plays where it
//                    always did
class Verification final : private juce::Timer, private juce::ChangeListener
{
public:
    enum class Run
    {
        list,
        reopen,
        legacy,
        file
    };

    struct Wiring
    {
        domain::CommandBus& bus;
        const domain::ProjectState& state;
        ui::WorkspaceView& view;
        ui::History& history;
        ui::CopilotHost& copilot;
        ui::Selection& selection;
        const ui::TransportClock& clock;
        const ui::Tokens& tokens;
        tracktion::Edit& edit;
        ui::SampleHost& samples;
        LevelMonitor& levels;

        // The whole window: the title bar above the workspace. Snapshots are
        // taken of it, and the File shortcuts are pressed on it.
        juce::DocumentWindow& window;
        juce::Component& shell;
        ui::TitleBarView& titleBar;

        juce::File folder;
        Run run{Run::list};

        // Called once the report is written, on the message thread.
        std::function<void(bool passed)> finished;

        // What the File menu does once its dialog has answered.
        std::function<bool(const juce::File&)> newProjectAt;
        std::function<bool(const juce::File&)> openProjectAt;
        std::function<bool(const juce::File&)> saveAsTo;
        std::function<juce::String()> lastRefusal;

        // Fichier > Exporter...: its dialog is answered like any other; the
        // files it writes land in the run's folder, not behind a save dialog.
        SongExporter* exporter{nullptr};

        // The instrument of the intermittent "no effect during playback":
        // its state is written next to every failed check, and every refusal
        // the bus made during a step is written in that step.
        const PlaybackProbe* probe{nullptr};
    };

    explicit Verification(Wiring wiring);
    ~Verification() override;

    Verification(const Verification&) = delete;
    Verification& operator=(const Verification&) = delete;
    Verification(Verification&&) = delete;
    Verification& operator=(Verification&&) = delete;

    void start();

private:
    struct Step
    {
        std::string title;
        std::function<void()> act;

        // Polled until true, or until the step times out.
        std::function<bool()> ready;
        double timeoutMs{4000.0};
    };

    // What an offline render sounded like: how long, and at which steps a
    // note started.
    struct Heard
    {
        double seconds{0.0};
        std::vector<int> onsets;
        std::vector<float> onsetLevels; // RMS of the sixteenth each onset starts in
    };

    void timerCallback() override;

    // Every reading of the master meter while recordingMaster_ is set, at the
    // rate the meters are refreshed: what the copilot reads is one of them.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void buildList();
    void buildReopen();
    void buildLegacy();
    void buildFile();

    // The meters, live and rendered, and the copilot reading them. Part of
    // the list, after the samples: a project with a sampler channel and a
    // clip on a companion track is the one that has something to measure.
    void addMeterSteps();

    // The mixer: F10, a bus, a route, a fader, a solo while playing, the
    // readiness check, and all of it undone.
    void addMixerSteps();

    // Ctrl + click, Ctrl+C, Ctrl+V, Ctrl+B in the piano roll and the rack.
    void addClipboardSteps();

    // A click on a sample of the browser plays it, and nothing else.
    void addAuditionSteps();
    void clickBrowserSample(const juce::String& name);
    [[nodiscard]] juce::Point<int> rackChannel(int row) const;
    [[nodiscard]] juce::Component* mixerStrip(const domain::TrackId& id) const;
    [[nodiscard]] double renderedMasterPeakDb(const std::string& name);

    // The tempo and the signature readouts: wheel, menu, typing, and the
    // tempo lane "Automatiser le tempo" opens in the playlist.
    void addTempoSteps();

    // The browser: one click opens a folder, the search finds "kick house"
    // and what is close to it.
    void addSearchSteps();

    // The velocity lane: a click, a crescendo drawn in one stroke and heard,
    // a stroke limited to the picked notes.
    void addVelocitySteps();

    // The rack without its grid: "+ Instrument", rename, remove, and a click
    // where the steps were that writes nothing.
    void addRackSteps();

    // S13: the pattern chosen in the transport, the channel in the piano roll.
    void addWorkflowSteps();

    // S13: the wheel over the ruler zooms, the middle button drags the view.
    void addNavigationSteps();

    // S13: Fichier > Exporter..., in the four formats, read back and measured.
    void addExportSteps();
    void openExportDialog();
    [[nodiscard]] std::vector<int> kickVelocities() const;
    [[nodiscard]] juce::TreeViewItem* browserItem(const juce::File& file) const;
    void clickBrowserItem(juce::TreeViewItem& item);
    [[nodiscard]] juce::TextEditor* browserSearch() const;
    void chooseMenuItem(int position);
    void answerDialog(const juce::String& field, const juce::String& typed);

    // The playlist on a song longer than the screen: scroll, zoom, follow.
    void addPlaylistViewSteps();

    // What the blocks show: previews built only when notes change, waveforms
    // measured once per sample and off the message thread.
    void addPreviewSteps();
    static void writeSong(const juce::File& file, double seconds);
    [[nodiscard]] engine::StripLevel levelOf(const std::string& strip) const;
    [[nodiscard]] static engine::StripLevel levelIn(const std::vector<engine::StripLevel>& levels,
                                                    const std::string& strip);
    void dragWindow(juce::Point<int> by);
    void add(std::string title,
             std::function<void()> act,
             std::function<bool()> ready = {},
             double timeoutMs = 4000.0);

    void check(bool passed, const std::string& what);
    void note(const std::string& what);
    void snapshot(const std::string& name);
    // An onset is a sixteenth louder than `floor` times the loudest one, and
    // than the sixteenth before it.
    [[nodiscard]] Heard listen(const std::string& name, double beatsPerMinute, float floor = 0.25f);
    void finish();

    // --- the doors a person uses
    [[nodiscard]] juce::Component* panel(const char* id) const;
    [[nodiscard]] juce::Button* button(juce::Component& root, const juce::String& text) const;
    void press(const juce::String& text);
    void click(juce::Component& target,
               juce::Point<int> at,
               bool right = false,
               bool shift = false,
               bool ctrl = false);
    void drag(juce::Component& target,
              juce::Point<int> from,
              juce::Point<int> to,
              bool ctrl = false,
              bool middle = false);

    // A short burst and silence, as a WAV file: a drum hit an onset detector
    // cannot miss.
    void doubleClick(juce::Component& target, juce::Point<int> at);

    static void writeHit(const juce::File& file, double seconds);
    void key(const juce::KeyPress& press);

    // Writes notes on a channel the way a person does since S12: the channel
    // clicked in the rack, a click per note in the piano roll.
    void writeNotes(int row, const std::vector<double>& beats);
    // Where a beat of a lane is, brought into sight first with the wheel the
    // way a person scrolls, when the view does not show it.
    [[nodiscard]] juce::Point<int> playlistBeat(int lane, double beats);
    void
    wheel(juce::Component& target, juce::Point<int> at, float deltaY, bool shift = false, bool ctrl = false);

    [[nodiscard]] std::size_t depth() const { return bus_.undoDepth(); }

    domain::CommandBus& bus_;
    const domain::ProjectState& state_;
    ui::WorkspaceView& view_;
    ui::History& history_;
    ui::CopilotHost& copilot_;
    ui::Selection& selection_;
    const ui::TransportClock& clock_;
    const ui::Tokens& tokens_;
    tracktion::Edit& edit_;
    ui::SampleHost& samples_;
    LevelMonitor& levels_;
    juce::DocumentWindow& window_;
    juce::Component& shell_;
    ui::TitleBarView& titleBar_;
    juce::File folder_;
    Run run_;
    std::function<void(bool)> finished_;

    std::vector<Step> steps_;
    std::size_t current_{0};
    bool acted_{false};
    bool timedOut_{false};

    // Per step: whether the probe was already written after a failure, and
    // how many refusals the bus had made when the step began.
    bool probed_{false};
    std::size_t refusalsAtStart_{0};
    double startedAtMs_{0.0};
    int settle_{0};

    juce::StringArray report_;
    int passed_{0};
    int failed_{0};

    // Carried from one step to the next.
    std::string savedState_;
    std::size_t savedDepth_{0};
    std::size_t transcriptBefore_{0};
    juce::File kit_;
    double audioStart_{0.0};
    juce::Rectangle<int> savedBounds_;
    std::string loudest_;
    std::vector<float> masterSeen_;
    double farBeats_{0.0};
    double tempoBefore_{0.0};
    double wheelAt_{0.0};
    Heard heardBefore_{};
    std::size_t tracksBefore_{0};
    SongExporter* exporter_{nullptr};
    juce::File exportsBefore_;
    double exportSeconds_{0.0};
    double exportRmsDb_{0.0};
    std::size_t previewBuilds_{0};
    std::size_t measuredBefore_{0};
    bool watchTicks_{false};
    double lastTickMs_{0.0};
    double longestTickMs_{0.0};
    double measuringSince_{0.0};
    bool recordingMaster_{false};
    std::size_t droppedBefore_{0};
    std::function<bool(const juce::File&)> newProjectAt_;
    std::function<bool(const juce::File&)> openProjectAt_;
    std::function<bool(const juce::File&)> saveAsTo_;
    std::function<juce::String()> lastRefusal_;
    const PlaybackProbe* probe_{nullptr};
};

} // namespace daw::app
