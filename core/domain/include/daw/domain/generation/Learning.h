#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/project/ProjectState.h"

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace daw::domain::generation
{

// The generator learns from the person who uses it.
//
// What it reads is what they wrote in the DAW: the notes of the patterns of
// their projects, counted in the spelling of StyleModel.h, with the rules of
// services/harmony/corpus.py -- the same counts, written a second time here so
// that learning needs no Python process, and pinned to the Python ones by the
// same fixture (GenerationTests, LearningTests).
//
// What it does not read: notes the generator or the copilot wrote that nobody
// touched since. Counting them would teach the model its own proposals, and it
// would drift back to where it started. A note the person moved, changed or
// wrote over counts: that is theirs.
//
// Nothing here leaves the machine. The counts are written next to the
// application's settings, never into a project, never to the copilot.

// Running moments of the velocity at one sixteenth: counts from several
// projects add up, a mean and a deviation would not.
struct Moments
{
    double count{0.0};
    double sum{0.0};
    double squares{0.0};

    friend bool operator==(const Moments& lhs, const Moments& rhs) = default;
};

struct RoleCounts
{
    RoleStyle tables; // the velocity of RoleStyle is left empty here
    std::map<int, Moments> velocity;
    double notes{0.0}; // how many events were counted, weighted

    // The velocity as the corpus pipeline writes it: mean, and the population
    // deviation, 8 for a single value, 2 at least.
    [[nodiscard]] std::map<int, std::pair<double, double>> velocityStyle() const;

    friend bool operator==(const RoleCounts& lhs, const RoleCounts& rhs) = default;
};

// What a set of projects taught, role by role.
struct Learned
{
    static constexpr std::string_view format = "daw-ia.learned";
    static constexpr int version = 1;

    std::array<RoleCounts, 4> roles{};

    [[nodiscard]] const RoleCounts& role(Role role) const noexcept;
    [[nodiscard]] RoleCounts& role(Role role) noexcept;
    [[nodiscard]] double notes() const noexcept;

    // Adds another's counts, multiplied by a weight: the current project
    // counts more than the others.
    void add(const Learned& other, double weight);

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Learned> fromValue(const Value& value);

    friend bool operator==(const Learned& lhs, const Learned& rhs) = default;
};

// --- counting, the rules of corpus.py ----------------------------------------

// One onset of a line: the highest note for a melody, the lowest for a bass.
struct LineEvent
{
    int step{0};   // in sixteenths from the start of the pattern
    int length{1}; // in sixteenths, one at least
    int pitch{60};
    int velocity{100};
};

// count_line: rhythm, duration and velocity for every role, degree and
// interval for a pitched one.
void countLine(RoleCounts& counts, const std::vector<LineEvent>& events, Key key, int barSteps, Role role);

// count_chords: the comping rhythm, and one chord per bar read with the
// generator's own rule.
void countChords(RoleCounts& counts, const std::vector<Note>& notes, Key key, int barSteps);

// The role a row is counted as. The rule of corpus.py's classify: a sample
// channel or a row of one pitch is rhythm, then the name, then three notes at
// once for chords, then the register.
[[nodiscard]] Role roleOfRow(const Track& track, const std::vector<Note>& notes);

// Every pattern of the project, row by row, in the key of its pattern (read
// from its pitched rows, else from the whole project, else A minor). A
// pattern laid ten times in the song counts once: it was written once.
[[nodiscard]] Learned learn(const ProjectState& state, const std::map<std::string, Note>& excluded = {});

// --- what the machine wrote -------------------------------------------------

// The notes added by the generator or the copilot, by identifier, as they were
// written. A note of the project still equal to its entry here was never
// touched by the person, and is not counted.
class MachineNotes
{
public:
    // From the journal of a project just opened (CommandBus::journal).
    void read(const std::vector<Value>& journal);

    // From then on, receipt by receipt.
    void observe(const Receipt& receipt);

    [[nodiscard]] const std::map<std::string, Note>& notes() const noexcept { return notes_; }

private:
    void take(std::string_view type, const Provenance& origin, const Value& payload);

    std::map<std::string, Note> notes_;
};

// --- mixing what was learned with the base ----------------------------------

// How much a base weighs against what was learned: as many notes as this, at
// every context. A person who wrote 200 notes has a style half theirs.
inline constexpr double baseWeight = 200.0;

// The current project counts this much more than the other projects.
inline constexpr double currentProjectWeight = 3.0;

// The share of the learned counts in the model, for one role: N / (N + base).
// It moves with every note written, never in a jump.
[[nodiscard]] double learnedShare(const Learned& learned, Role role, double base = baseWeight) noexcept;

// What was learned, with the base as a prior worth `baseMass` notes against
// the notes learned: at every context the person played, the learned share is
// learnedShare(). A context they never played keeps the base's answer. With
// nothing learned, the base itself.
[[nodiscard]] StyleModel blend(const StyleModel& base, const Learned& learned, double baseMass = baseWeight);

} // namespace daw::domain::generation
