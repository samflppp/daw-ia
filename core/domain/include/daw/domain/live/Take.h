#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/live/Router.h"
#include "daw/domain/project/ProjectState.h"

#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace daw::domain::live
{

// A take (S23): what was played while recording, written into the project as
// existing commands — pattern.add_track when the row is missing, note.add for
// each note, plus pattern.create and pattern.place in song mode —, every
// identifier given by the caller, in one group, at the end of the take. One
// Ctrl+Z takes the whole take back, to the byte. Nothing is written while it
// lasts: the screen shows the notes coming, from notes().
//
// Where a note is written: where it was heard. A key pressed when the person
// hears the downbeat is pressed when the engine has already rendered a little
// further — the sound card's output latency. The note goes back by that much.
// Nothing is quantised: a take written stays chosen, and Ctrl+Q quantises it.
//
// The pedal lengthens: a note released while the pedal is down ends when the
// pedal comes up, the way it was heard. The model of a note keeps no pedal,
// no bend, no modulation; those are played, not written.

struct TakeNote
{
    TrackId track;
    int pitch{60};
    int velocity{100};
    double startBeats{0.0};
    double lengthBeats{0.0};
    bool open{false}; // still held: its length is the length so far

    friend bool operator==(const TakeNote& lhs, const TakeNote& rhs) = default;
};

struct TakeTiming
{
    // How far what is heard is behind what is rendered: the sound card's
    // output latency, its buffer included.
    double latencySeconds{0.0};

    // In pattern mode the take loops over the pattern: a note is written
    // where it falls in it, pass after pass. Zero: no loop (song mode).
    double loopStartBeats{0.0};
    double loopLengthBeats{0.0};

    // The Edit's seconds to beats: the tempo. Defined before zero too, which
    // a note played on the first downbeat, minus the latency, falls on.
    std::function<double(double)> beatsAt;
};

class TakeBuilder
{
public:
    explicit TakeBuilder(TakeTiming timing);

    // One event of the take (a note, its release, the pedal), the track it
    // went to, and the song's last known position.
    void add(const Event& event, const TrackId& track, const Position& position);

    // The take ends at `seconds`: what is still held ends there.
    void stop(double seconds, const Position& position);

    // The notes of the take so far, the held ones with their length at
    // `seconds`, in the order they were played.
    [[nodiscard]] std::vector<TakeNote> notes(double seconds, const Position& position) const;

    // Where a key pressed at `seconds` is written, in beats.
    [[nodiscard]] double beatsHeard(double seconds, const Position& position) const;

private:
    struct Held
    {
        TakeNote note;
        double heard{0.0};   // the Edit's seconds heard when it started, unwrapped
        double pressed{0.0}; // on the input clock
        bool released{false};
    };

    [[nodiscard]] double heardSeconds(double seconds, const Position& position) const;
    [[nodiscard]] double wrapped(double beats) const;
    [[nodiscard]] double lengthOf(const Held& held, double end) const;
    void close(std::size_t index, double end);

    TakeTiming timing_;
    std::vector<Held> held_;
    std::vector<std::pair<double, TakeNote>> closed_; // with the instant it was pressed
    std::map<TrackId, bool> pedal_;
};

// Where a take is written.
struct TakeDestination
{
    PlayMode mode{PlayMode::pattern};
    // Pattern mode: the auditioned pattern. Song mode: a pattern of its own,
    // laid at `atBeats` on a line of its own, `lengthBeats` long, the notes'
    // beats being those of the song.
    PatternId pattern;
    PlacementId placement;
    double atBeats{0.0};
    double lengthBeats{0.0};
};

// Every identifier a take needs, given by the caller before any command is
// built: rejouer le même payload donne le même projet.
struct TakeIds
{
    std::function<NoteId()> note;
    std::function<ClipId()> row;
};

// Two notes of one row closer than this, at the same pitch, are the same
// note: a second pass over a loop adds, it does not double a note.
inline constexpr double sameStartBeats = 1.0 / 128.0;

// In a loop, a note that starts this close before the end of the pattern is
// the next pass's first beat, played a hair early: it is written at the
// start. Written at the end, it would be cut by the end of the pattern to
// almost nothing. Not a quantisation: only the wrap is decided.
inline constexpr double wrapBeats = 1.0 / 32.0;
inline constexpr double shortestBeats = 1.0 / 128.0;

// The commands of a take, in the order to execute them as one group. Empty
// when there is nothing to write.
[[nodiscard]] std::vector<std::unique_ptr<Command>> takeCommands(const ProjectState& state,
                                                                 const TakeDestination& destination,
                                                                 const std::vector<TakeNote>& notes,
                                                                 const TakeIds& ids);

} // namespace daw::domain::live
