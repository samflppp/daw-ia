#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::domain::generation
{

// What the generator is asked for, and nothing about how it answers.
//
// This is the contract between two weeks. In S14 the zone above the piano roll
// is read by a local, deterministic interpreter that knows a dozen short words.
// In S15 the remote copilot will turn "une basse sombre en doubles-croches"
// into the same structure. The zone does not change, the generator does not
// change: only the interpreter does. That is why the structure has a Value
// form -- it is what a Python process will send back.
//
// Every field is optional, and empty means "deduce it from the context", never
// "whatever". A constraint the user did not give is a constraint the generator
// reads from the notes around the range, and says it did.

enum class Mode : std::uint8_t
{
    major,
    minor // natural minor
};

enum class Resolution : std::uint8_t
{
    quarter,  // noires
    eighth,   // croches
    sixteenth // doubles-croches
};

enum class Density : std::uint8_t
{
    sparse, // clair
    medium,
    dense
};

enum class Register : std::uint8_t
{
    low, // grave
    mid,
    high // aigu
};

enum class Role : std::uint8_t
{
    melody,
    bass,
    chords,
    rhythm // a sample channel: positions and velocities, the pitch is the channel's
};

struct Key
{
    int tonic{9}; // pitch class, 0 = C
    Mode mode{Mode::minor};

    friend bool operator==(const Key& lhs, const Key& rhs) = default;
};

struct Constraints
{
    std::optional<Key> key;
    std::optional<Resolution> resolution;
    std::optional<Density> density;
    std::optional<Register> reg;
    std::optional<Role> role;

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Constraints> fromValue(const Value& value);

    friend bool operator==(const Constraints& lhs, const Constraints& rhs) = default;
};

// What an interpreter made of a text: the constraints, and every word it did
// not use, said rather than guessed.
struct Interpretation
{
    Constraints constraints;
    std::vector<std::string> ignored;   // words that are no constraint: "sombre"
    std::vector<std::string> conflicts; // "croches puis doubles : doubles retenu"

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Interpretation> fromValue(const Value& value);

    friend bool operator==(const Interpretation& lhs, const Interpretation& rhs) = default;
};

// Takes a text, gives back constraints. Asynchronous in shape from the first
// day: the remote interpreter of S15 answers in seconds, and a caller written
// for an immediate answer would have to be rewritten for it. The local one
// calls done before returning.
class ConstraintInterpreter
{
public:
    virtual ~ConstraintInterpreter() = default;
    virtual void interpret(std::string_view text, std::function<void(Interpretation)> done) = 0;
};

// The S14 interpreter: short words separated by spaces, case ignored.
//
//   tonalité    Am, F#m, Bbm, C, Eb (m = minor)
//   résolution  noires, croches, doubles (doubles-croches, 1/4, 1/8, 1/16)
//   densité     clair, moyen, dense
//   registre    grave, medium, aigu
//   rôle        mélodie, basse, accords, rythme
//
// Anything else is ignored and reported. Two words for one field: the last
// one wins, and the conflict is reported.
class LocalInterpreter final : public ConstraintInterpreter
{
public:
    void interpret(std::string_view text, std::function<void(Interpretation)> done) override;

    [[nodiscard]] static Interpretation parse(std::string_view text);
};

// --- once resolved ---------------------------------------------------------

// Where a resolved value comes from. The zone shows it next to the value: a
// key the user typed and a key the generator guessed are not the same promise.
enum class Source : std::uint8_t
{
    imposed,  // written by the user
    deduced,  // read from the notes around the range
    defaulted // nothing to read it from
};

template <typename T>
struct Resolved
{
    T value{};
    Source source{Source::defaulted};

    friend bool operator==(const Resolved& lhs, const Resolved& rhs) = default;
};

struct ResolvedConstraints
{
    Resolved<Key> key;
    Resolved<Resolution> resolution;
    Resolved<Density> density;
    Resolved<Register> reg;
    Resolved<Role> role;

    friend bool operator==(const ResolvedConstraints& lhs, const ResolvedConstraints& rhs) = default;
};

// French words, for the zone and the history label.
[[nodiscard]] std::string describe(Key key);
[[nodiscard]] std::string_view describe(Resolution resolution) noexcept;
[[nodiscard]] std::string_view describe(Density density) noexcept;
[[nodiscard]] std::string_view describe(Register reg) noexcept;
[[nodiscard]] std::string_view describe(Role role) noexcept;
[[nodiscard]] std::string_view describe(Source source) noexcept;

// "La mineur (déduit) · doubles (imposé) · basse (déduit)"
[[nodiscard]] std::string describe(const ResolvedConstraints& constraints);

// "Am doubles basse": the imposed values only, short, for the history label.
[[nodiscard]] std::string shortLabel(const ResolvedConstraints& constraints);

// The pitch-class name a key is written with: "A", "F#", "Bb".
[[nodiscard]] std::string_view tonicName(int pitchClass) noexcept;

} // namespace daw::domain::generation
