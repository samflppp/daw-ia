#pragma once

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/Tokens.h"
#include "daw/ui/WorkspaceView.h"
#include "daw/ui/model/CopilotHost.h"
#include "daw/ui/model/History.h"
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
// SONG and "+ Pattern" buttons, Ctrl+Z through the view, a sentence typed to
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
class Verification final : private juce::Timer
{
public:
    enum class Run
    {
        list,
        reopen,
        legacy
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
        juce::File folder;
        Run run{Run::list};

        // Called once the report is written, on the message thread.
        std::function<void(bool passed)> finished;
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
    };

    void timerCallback() override;

    void buildList();
    void buildReopen();
    void buildLegacy();
    void add(std::string title,
             std::function<void()> act,
             std::function<bool()> ready = {},
             double timeoutMs = 4000.0);

    void check(bool passed, const std::string& what);
    void note(const std::string& what);
    void snapshot(const std::string& name);
    [[nodiscard]] Heard listen(const std::string& name, double beatsPerMinute);
    void finish();

    // --- the doors a person uses
    [[nodiscard]] juce::Component* panel(const char* id) const;
    [[nodiscard]] juce::Button* button(juce::Component& root, const juce::String& text) const;
    void press(const juce::String& text);
    void click(juce::Component& target, juce::Point<int> at, bool right = false, bool shift = false);
    void drag(juce::Component& target, juce::Point<int> from, juce::Point<int> to);
    void key(const juce::KeyPress& press);

    // Where a cell of the rack and a beat of a playlist lane are, read from the
    // same tokens the panels draw with.
    [[nodiscard]] juce::Point<int> rackCell(int row, int step) const;
    [[nodiscard]] juce::Point<int> playlistBeat(int lane, double beats) const;

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
    juce::File folder_;
    Run run_;
    std::function<void(bool)> finished_;

    std::vector<Step> steps_;
    std::size_t current_{0};
    bool acted_{false};
    bool timedOut_{false};
    double startedAtMs_{0.0};
    int settle_{0};

    juce::StringArray report_;
    int passed_{0};
    int failed_{0};

    // Carried from one step to the next.
    std::string savedState_;
    std::size_t savedDepth_{0};
    std::size_t transcriptBefore_{0};
};

} // namespace daw::app
