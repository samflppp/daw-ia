#pragma once

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/live/Router.h"
#include "daw/domain/live/Take.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace daw::app
{

// Recording what is played (S23), on the message thread.
//
// ● (or Ctrl+R) starts a take: a bar of count-in with the click, then what
// the keys play is kept, and the notes are seen coming into the canvas
// without being in the project yet. ● again, or Space, ends it: the take is
// written in one group (domain::live::takeCommands), one Ctrl+Z.
//
// Where: in pattern mode, the auditioned pattern, which loops — each pass
// adds to the last; in song mode, a pattern of its own laid at the bar where
// the take began. When: where each note was heard — the card's output
// latency taken back. The count-in starts the song a bar before (in pattern
// mode, on the pattern's last bar); at the very start of a song there is no
// bar before, and the count-in is skipped: the domain refuses a playhead
// before zero.
//
// The metronome is Tracktion's click: a property of the Edit that does not
// come from the project (an écart, dit dans le bilan) — it is a setting of
// the screen, like the mode of the keyboard.
class TakeRecorder final : private juce::Timer
{
public:
    enum class Stage
    {
        idle,
        counting, // the bar before: heard, not written
        recording
    };

    struct Wiring
    {
        domain::live::Router& router;
        domain::CommandBus& bus;
        const domain::ProjectState& state;
        tracktion::Edit& edit;
        // The output latency of the card, in seconds, its buffer included;
        // the engine's in the application, a known one in a verification.
        std::function<double()> latency;
    };

    explicit TakeRecorder(Wiring wiring);
    ~TakeRecorder() override;

    TakeRecorder(const TakeRecorder&) = delete;
    TakeRecorder& operator=(const TakeRecorder&) = delete;
    TakeRecorder(TakeRecorder&&) = delete;
    TakeRecorder& operator=(TakeRecorder&&) = delete;

    // Starts a take, or ends the one going on.
    void toggle();
    void start();
    void stop();

    [[nodiscard]] Stage stage() const noexcept { return stage_; }

    // The notes of the take so far, for the canvas: by track, in the beats of
    // the pattern written into (pattern mode) or of the song (song mode).
    [[nodiscard]] std::vector<domain::live::TakeNote> notes() const;
    [[nodiscard]] domain::PlayMode mode() const noexcept { return mode_; }
    [[nodiscard]] domain::PatternId pattern() const noexcept { return pattern_; }

    // What the last take wrote, or why it wrote nothing, in French.
    [[nodiscard]] const std::string& said() const noexcept { return said_; }

    // How many notes the last take wrote.
    [[nodiscard]] int written() const noexcept { return written_; }

    // The click, during takes and their count-in, or always.
    void setMetronome(bool on);
    [[nodiscard]] bool metronome() const noexcept { return metronome_; }

    // A bar of count-in before a take, or none.
    void setCountIn(bool on) noexcept { countIn_ = on; }
    [[nodiscard]] bool countIn() const noexcept { return countIn_; }

    // Told when the stage changes, or a take is written.
    std::function<void()> onChanged;

    // Reads the router now rather than at the next tick.
    void poll();

private:
    void timerCallback() override;
    void finish();
    void applyClick();
    void changed();
    [[nodiscard]] double secondsOf(double beats) const;
    [[nodiscard]] double beatsOf(double seconds) const;

    Wiring wiring_;
    Stage stage_{Stage::idle};
    domain::PlayMode mode_{domain::PlayMode::pattern};
    domain::PatternId pattern_{};
    std::unique_ptr<domain::live::TakeBuilder> take_;

    // The take begins when what is heard reaches this beat (the end of the
    // count-in); the input clock's instant of it, once known.
    double startBeats_{0.0};
    double prerollBeats_{0.0};
    std::optional<double> startClock_;
    bool startedTransport_{false};

    bool metronome_{false};
    bool countIn_{true};
    std::string said_;
    int written_{0};
};

} // namespace daw::app
