#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/project/ProjectState.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daw::domain::generation
{

// A proposed note. No identifier: it is not in the project and may never be.
// The identifiers are drawn when the proposal is accepted, and not before,
// because a proposal rejected must leave no trace -- not even a used id.
struct GhostNote
{
    int pitch{60};
    int velocity{100};
    double startBeats{0.0};
    double lengthBeats{0.25};

    friend bool operator==(const GhostNote& lhs, const GhostNote& rhs) = default;
};

// Everything the generator reads, copied out of the project. It holds no
// pointer into ProjectState: a proposal outlives the command that changes the
// notes it was made from, and compares its hash to know it.
struct Context
{
    double patternLength{16.0};
    double beatsPerBar{4.0};
    double fromBeats{0.0};
    double toBeats{16.0};

    std::vector<Note> row;     // the row being written, all of it
    std::vector<Note> harmony; // the other pitched rows of the pattern
    std::vector<Note> project; // every pitched note of the project, for the key

    std::string trackName;
    bool sampleChannel{false};
    int channelPitch{60};

    // Built from the project. The range is clamped to the pattern and the
    // grid; an empty range is an error.
    [[nodiscard]] static Result<Context>
    of(const ProjectState& state, PatternId pattern, TrackId track, double fromBeats, double toBeats);

    // Stable across runs and machines: the proposal compares it after every
    // change of the project to know whether it still describes what is there.
    [[nodiscard]] std::uint64_t hash() const;
};

// Fills every field the user left empty, from the context.
[[nodiscard]] ResolvedConstraints resolve(const Constraints& constraints, const Context& context);

// The chord each bar of the range sits on, read from the other rows. Nothing
// for a bar where they play nothing in the key.
[[nodiscard]] std::vector<std::optional<Chord>> chordsOf(const Context& context, Key key);

// One variant. The same context, constraints, model and variant give the same
// notes, on any machine: the random draws come from a seed built from all four,
// through a generator written here and not from the standard library, whose
// distributions differ between implementations.
[[nodiscard]] std::vector<GhostNote> generate(const Context& context,
                                              const ResolvedConstraints& constraints,
                                              const StyleModel& model,
                                              int variant);

// Variants, drawn on demand, with duplicates skipped: Alt + wheel never shows
// the same notes twice in a row.
class Variants
{
public:
    static constexpr int maxVariants = 16;

    Variants(Context context, ResolvedConstraints constraints, const StyleModel& model);

    // The variant at that rank, 0 first. Clamped to what could be drawn.
    [[nodiscard]] const std::vector<GhostNote>& at(int rank);

    // How many distinct variants exist so far.
    [[nodiscard]] int drawn() const noexcept { return static_cast<int>(drawn_.size()); }

    [[nodiscard]] const Context& context() const noexcept { return context_; }
    [[nodiscard]] const ResolvedConstraints& constraints() const noexcept { return constraints_; }

private:
    Context context_;
    ResolvedConstraints constraints_;
    const StyleModel& model_;
    std::vector<std::vector<GhostNote>> drawn_;
    int nextSeed_{0};
};

} // namespace daw::domain::generation
