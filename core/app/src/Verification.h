#pragma once

#include "LevelMonitor.h"
#include "PlaybackProbe.h"
#include "SongExporter.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/live/Router.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/engine/AudioOutputKeeper.h"
#include "daw/engine/AudioSettings.h"
#include "daw/ui/TitleBarView.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/WorkspaceView.h"
#include "daw/ui/model/CopilotHost.h"
#include "daw/ui/model/History.h"
#include "daw/ui/model/ProjectObserver.h"
#include "daw/ui/model/SampleHost.h"
#include "daw/ui/model/Selection.h"
#include "daw/ui/model/TransportClock.h"
#include "daw/ui/model/WorkspaceHost.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace daw::engine
{
class ContentStore;
class LiveInputPlugin;
} // namespace daw::engine

namespace daw::ui
{
class GenerationPanel;
}

namespace daw::app
{

class LivePlay;
class BusSession;
class KitSession;
class MixSession;
class StemSession;
class VoiceInput;

// Where in a beat the first bar of the list is loud: its kick strikes on
// every beat and is gone before the next — at 90 BPM, the master's 300 ms
// peak falls under -60 dBFS from 0.75 of a beat to the next strike. A meter
// read there is the music, not a silent playback: in S24, all 114 silent
// readings of five --verify-lecture runs sat between 0.55 and 0.65 s, the
// end of the first beat; in S25, the two failures of --verify at steps 58
// and 60 read at 0.62 and 0.60 s. So a reading meant to hear the bar is the
// last one taken between these two places of a beat.
inline constexpr double loudFromBeat = 0.2;
inline constexpr double loudToBeat = 0.55;

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
//   --verify-fluidite  how long the interface takes to paint, on an empty
//                    project it fills first
//   --verify-stems   the stem separator with a real model, on known sources
//   --verify-lecture the list up to its meters, then the first bar looped
//                    and stopped sixty times, after four kinds of action:
//                    how often the song plays and is not heard (S21)
class Verification final : private juce::Timer, private juce::ChangeListener
{
public:
    enum class Run
    {
        list,
        reopen,
        legacy,
        file,
        canvas,
        canvasLoad,
        fluidity,
        mix,
        playback,
        stems,
        play,
        audio,
        flux,
        kit,
        voice
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

        // What the panels hear when the project changes. The fluidity run
        // hands its pending message over within an image, the way the
        // message loop would between two moves of the hand.
        ui::ProjectObserver* project{nullptr};

        // The mix by the AI (S20): --verify-mix drives it, by the rules only.
        MixSession* mix{nullptr};

        // The workspace switch, asked by identifier the way a shortcut or the
        // copilot would ask: a workspace reserved for workshops is refused.
        ui::WorkspaceHost* workspaces{nullptr};

        // The sound card kept open (S21): --verify-lecture loses it on purpose.
        engine::AudioOutputKeeper* output{nullptr};

        // The stem separator (S22): --verify-stems drives it with the fast model.
        StemSession* stems{nullptr};

        // Playing live and recording (S23): --verify-jeu presses the keys.
        LivePlay* live{nullptr};
        domain::live::Router* router{nullptr};

        // The sound card's settings (S24): --verify-audio drives the window.
        engine::AudioSettings* audio{nullptr};

        // The kit (S24): --verify-kit indexes a library it builds.
        KitSession* kit{nullptr};
        // The smart buses (S24): --verify-flux tries and keeps one.
        BusSession* buses{nullptr};

        // The push-to-talk (S25): --verify-voix holds its key, plays files
        // where the microphone would be, and reads what the store kept.
        VoiceInput* voice{nullptr};
        engine::ContentStore* store{nullptr};
        // --voix-micro-reel: opens this machine's microphone too, to measure
        // that the output does not change when it opens.
        bool realMicrophone{false};
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
    // While recordingPlayback_ is set, each reading taken as the transport
    // plays, with the position it was taken at (--verify-lecture). While
    // recordingStrips_ is set, every strip's reading, with its position
    // (the meters of --verify, S26).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void buildList();

    // S23 (VerificationLive.cpp): playing live and recording, without a
    // keyboard plugged in.
    struct PlayRun;
    void buildPlay();

    // S24 (VerificationAudio.cpp): the « Audio » window on this card.
    struct AudioRun;
    void buildAudio();

    // S24 (VerificationFlux.cpp): the effects from the mixer's strip, and
    // the audio flux.
    struct FluxRun;
    void buildFlux();
    void addFluxWindow(const std::shared_ptr<FluxRun>& run);

    // S24 (VerificationKit.cpp): the kit, on a library built here.
    struct KitRun;
    void buildKit();

    struct VoiceRun;
    void buildVoice();
    // An offline render of the Edit as it plays, read back; and the type of
    // the last command the journal holds.
    [[nodiscard]] juce::AudioBuffer<float> renderNamed(const std::string& name);
    [[nodiscard]] std::string lastCommandType() const;
    [[nodiscard]] engine::LiveInputPlugin* liveInputOf(const domain::TrackId& track) const;
    void buildReopen();
    void buildLegacy();
    void buildFile();
    void buildCanvas();
    void buildCanvasLoad();

    // S18 bis (VerificationFluidity.cpp): repaints timed on a filled project,
    // the playlist, the piano roll and the mixer whole, and sixty images of an
    // internal window dragged over the playlist. Median and 95th percentile.
    void buildFluidity();

    // S20 (VerificationMix.cpp): the mix by the AI on known signals, without
    // a key — measures against the numbers the signals must give, the guards,
    // before and after at equal loudness, one entry by the copilot, Ctrl+Z to
    // the byte, a strip refused, a cancel, a reference.
    void buildMix();
    void buildStems();

    // S21 (VerificationPlayback.cpp): the silent playback of S12 and S20,
    // reproduced. Seventy-five cycles of the looped first bar, each after one
    // of five actions — nothing, an offline render of the live Edit, a
    // command, a sample auditioned, the sound card lost — and when one is
    // silent, the audio path described twice, half a second apart. The report
    // ends on the count per action.
    void addPlaybackCycles();

    // S21 (VerificationWindows.cpp), at the end of --verify-canvas: a window
    // dragged lands on an edge, a shared edge moves both windows, the rack
    // and the history scroll.
    void addWindowSteps();

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

    // S13: the automation (VerificationAutomation.cpp). The copilot's
    // fade-out, the right-click on a slider, points laid with the mouse, each
    // heard window by window on a render; then the disorder: the tempo
    // changed under the points, undo and redo while the song plays.
    void addAutomationSteps();

    // S14: generation in the piano roll (VerificationGeneration.cpp). A range
    // on the ruler, Ctrl+G, constraints typed, variants, the disorder --
    // reject, generate twice, change pattern, undo something else, generate
    // while the song plays -- with the project unmoved throughout; then Tab,
    // and the render: attacks and pitches.
    void addGenerationSteps();

    // S15: the form. Four bars on a fresh channel: AABA deduced and shown,
    // bars 1, 2 and 4 on one rhythm and bar 3 on another, on screen and once
    // written; "boucle" and "libre" understood.
    void addFormSteps();

    // S15: the generator learns from the person, in the verification's own
    // folder: the base first, the generator's notes teaching nothing, the
    // person's eighths learned and measured, the save, the menu, forgetting.
    void addLearningSteps();

    // S17: the zone of the playlist over three lines named Accords, Basse and
    // Mélodie. Alt + drag, Ctrl+G, one prompt; three parts in the roles the
    // names say, each on its line and its track; heard before being written;
    // Tab is one entry, and one Ctrl+Z takes the three away.
    void addZoneSteps();

    // S18: the canvas (VerificationCanvas.cpp). F4, a block framed at the
    // scale of notes, a note written and moved in a band, the other block of
    // the pattern lit and showing it; then the repaint of a loaded project
    // at three scales and during a pan.
    void addCanvasSteps();

    // S18: the canvas on a loaded project, its repaint measured at three
    // scales and during a pan. Its own run (--verify-canvas-charge): laying
    // a thousand clips in the engine takes minutes in a debug build.
    void addCanvasLoadSteps();

    // A render of the Edit as it plays, kept whole in memory, and its level
    // window by window between two beats of the song: the RMS of each
    // channel, in dB.
    struct Rendered
    {
        juce::AudioBuffer<float> audio;
        double sampleRate{0.0};
    };
    struct Window
    {
        double fromBeats{0.0};
        double leftDb{0.0};
        double rightDb{0.0};
        [[nodiscard]] double meanDb() const;
    };
    [[nodiscard]] Rendered render(const std::string& name);
    [[nodiscard]] std::vector<Window>
    windowsOf(const Rendered& rendered, double fromBeats, double toBeats, double windowBeats) const;
    [[nodiscard]] double songEndBeats() const;
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
              bool middle = false,
              bool shift = false,
              bool alt = false);

    // A short burst and silence, as a WAV file: a drum hit an onset detector
    // cannot miss.
    void doubleClick(juce::Component& target, juce::Point<int> at);
    void doubleClickCaption();

    static void writeHit(const juce::File& file, double seconds);
    void key(const juce::KeyPress& press);

    // Types the words in a generation window and presses Enter, then waits
    // until the prompt is read. With the copilot's process connected, the
    // reading goes through it and answers later, on the message thread: a
    // step that read the screen in the same call read the previous gesture.
    void prompt(ui::GenerationPanel& bar, const juce::String& words);

    // Writes notes on a channel the way a person does since S12: the channel
    // clicked in the rack, a click per note in the piano roll.
    void writeNotes(int row, const std::vector<double>& beats);
    // Where a beat of a lane is, brought into sight first with the wheel the
    // way a person scrolls, when the view does not show it.
    [[nodiscard]] juce::Point<int> playlistBeat(int lane, double beats);
    void wheel(juce::Component& target,
               juce::Point<int> at,
               float deltaY,
               bool shift = false,
               bool ctrl = false,
               bool alt = false);

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

    // The windows meant to be shown that Windows does not show, as last said
    // (S25: a check that does not see a window shown says what Windows says).
    juce::String windowsUnseen_;
    int windowsUnseenTimes_{0};
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
    Rendered plain_{};
    double songEnd_{0.0};
    domain::AutomationLineId masterLine_{};
    domain::AutomationLineId panLine_{};
    std::string playedState_;
    std::string untouchedState_;
    std::size_t playedDepth_{0};
    double stepStartedMs_{0.0};
    std::size_t previewBuilds_{0};
    std::size_t measuredBefore_{0};
    bool watchTicks_{false};
    double lastTickMs_{0.0};
    double longestTickMs_{0.0};
    double measuringSince_{0.0};
    bool recordingMaster_{false};
    std::vector<std::pair<double, float>> playbackSeen_;
    bool recordingPlayback_{false};
    std::vector<std::pair<double, std::vector<engine::StripLevel>>> stripsSeen_;
    bool recordingStrips_{false};
    std::size_t droppedBefore_{0};
    std::function<bool(const juce::File&)> newProjectAt_;
    std::function<bool(const juce::File&)> openProjectAt_;
    std::function<bool(const juce::File&)> saveAsTo_;
    std::function<juce::String()> lastRefusal_;
    const PlaybackProbe* probe_{nullptr};
    ui::ProjectObserver* project_{nullptr};
    MixSession* mix_{nullptr};
    StemSession* stems_{nullptr};
    ui::WorkspaceHost* workspaces_{nullptr};
    engine::AudioOutputKeeper* output_{nullptr};
    LivePlay* live_{nullptr};
    domain::live::Router* router_{nullptr};
    engine::AudioSettings* audio_{nullptr};
    KitSession* kitSession_{nullptr};
    BusSession* busSession_{nullptr};
    VoiceInput* voice_{nullptr};
    engine::ContentStore* store_{nullptr};
    bool realMicrophone_{false};

    // S21: cycles run and silent ones, per action before the play.
    std::map<std::string, int> cyclesByAction_;
    std::map<std::string, int> silentByAction_;
    bool cycleSilent_{false};
    int reopenedBefore_{0};
    int historyScrolled_{0};

    // S14.
    domain::TrackId leadTrack_{};
    std::string generationBaseline_;
    std::size_t generationDepth_{0};
    std::vector<domain::generation::GhostNote> firstGhosts_;
    std::vector<domain::generation::GhostNote> acceptedGhosts_;
};

} // namespace daw::app
