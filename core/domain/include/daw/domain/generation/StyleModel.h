#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/generation/Constraints.h"

#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace daw::domain::generation
{

// The style layer: counts of what followed what in the corpus, per role. It
// ranks the candidates the rules let through, and it never sees any other.
//
// The tables are written by the corpus pipeline (services/harmony, Python)
// and read here. The contexts are strings so the two sides agree on one
// spelling, and the spelling is written once, below:
//
//   rhythm       "p<pos>|<ioi>,<ioi>,<ioi>"  -> next inter-onset interval
//                pos: the onset's sixteenth in the bar, 0..15; intervals in
//                sixteenths, oldest first, 1..16
//   duration     "<ioi>"                     -> length in sixteenths
//   interval     "<d>,<d>,<d>"               -> next interval in scale degrees,
//                                               -9..+9
//   degree       "s" or "w"                  -> degree 0..6 on a strong beat or
//                                               a weak sixteenth
//   progression  "<root>,<root>,<root>"      -> next chord root, 0..6
//
// Every shorter context is counted too, down to the empty one: "p4|2,2,1",
// "p4|2,1", "p4|1", "p4|", "|". That is what the backoff walks.
//
// Degrees and intervals are relative to the tonic. A corpus in F# minor and a
// corpus in A minor count the same thing, which is why no transposition is
// ever needed.

using Counts = std::map<int, double>;
using Table = std::map<std::string, Counts, std::less<>>;

struct RoleStyle
{
    Table rhythm;
    Table duration;
    Table interval;
    Table degree;
    Table progression;

    // Mean and deviation of the velocity at each sixteenth of the bar.
    std::map<int, std::pair<double, double>> velocity;
};

class StyleModel
{
public:
    static constexpr std::string_view format = "daw-ia.style";
    static constexpr int version = 1;

    // Written by hand: stepwise motion, chord tones on strong beats, onsets
    // that land on the beat. Correct and flat, and meant to be: it is what
    // plays when there is no corpus, and what the CI tests against.
    [[nodiscard]] static StyleModel fallback();

    [[nodiscard]] static Result<StyleModel> fromValue(const Value& value);
    [[nodiscard]] Value toValue() const;

    [[nodiscard]] const RoleStyle& role(Role role) const noexcept;

    // "corpus" or "repli".
    [[nodiscard]] const std::string& origin() const noexcept { return origin_; }

private:
    std::array<RoleStyle, 4> roles_{};
    std::string origin_{"repli"};
};

// The contexts of each table, longest first.
[[nodiscard]] std::vector<std::string> rhythmContexts(int position, const std::vector<int>& intervals);
[[nodiscard]] std::vector<std::string> durationContexts(int interval);
[[nodiscard]] std::vector<std::string> intervalContexts(const std::vector<int>& intervals);
[[nodiscard]] std::vector<std::string> degreeContexts(bool strong);
[[nodiscard]] std::vector<std::string> progressionContexts(const std::vector<int>& roots);

// How likely x is among `candidates` choices, after the contexts. Each longer
// context refines the shorter one, weighted by how much it has seen:
//
//   P_k(x) = (count_k(x) + alpha * P_{k-1}(x)) / (total_k + alpha)
//
// starting from the uniform choice. A context the corpus never saw changes
// nothing, a context it saw a thousand times decides, and nothing is ever
// zero: a legal candidate the corpus never played stays possible, rarely.
[[nodiscard]] double
smoothed(const Table& table, const std::vector<std::string>& contexts, int x, std::size_t candidates);

// The same, for every candidate at once: each context is found and summed
// once instead of once per candidate. This is the one the generator calls.
[[nodiscard]] std::vector<double> smoothed(const Table& table,
                                           const std::vector<std::string>& contexts,
                                           const std::vector<int>& candidates,
                                           std::size_t choices);

} // namespace daw::domain::generation
