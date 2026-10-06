#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/mix/Measurement.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/domain/tidy/Roles.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace daw::domain::mix
{

// What the mix decides (S20), apart from what it measures.
//
//   The measure  — Measurement.h — is code: it guesses nothing.
//   The decision — a model, or the rules of this file when there is none —
//                  chooses among what is allowed, from numbers only.
//   The guards   — this file — are code too: bounds the decision cannot
//                  cross, checked before anything is tried, and said back.
//
// The same split as the generator's (S14): what is right is decided by rules,
// what is chosen among it may be decided by a model.

// --- roles ---------------------------------------------------------------------

struct RoleGuess
{
    MixRole role{MixRole::fx};
    bool known{false};   // false: nothing said it, it is treated with care
    std::string because; // « choisi par toi », « d'après le nom « Kick » »
    bool chosen{false};  // the person chose it: it is in the project
};

// In order of trust: the role the person chose; what the names, the preset,
// the sample or the notes say (tidy::classifyTrack, S17); what the sound says
// (the measure: where its energy sits, how it hits). The model never decides
// a role.
[[nodiscard]] RoleGuess guessRole(const ProjectState& state,
                                  TrackId track,
                                  const StreamMeasure* measure,
                                  const tidy::PresetNames& presets = {});

[[nodiscard]] std::string roleLabel(MixRole role); // « kick », « basse », « voix »

// --- the axes ------------------------------------------------------------------

// Continuous, never a list of styles. Each from -1 to +1, 0 is neutral.
struct Axes
{
    double punch{0.0}; // propre (-1) ↔ percutant (+1): compression, drums up
    double focus{0.0}; // voix devant (-1) ↔ instru devant (+1)
    double width{0.0}; // serré (-1) ↔ large (+1): how far the sides are panned

    [[nodiscard]] Value toValue() const;
};

// What a mix aims at, beyond the balance of the roles: the spectral tilt of
// the master (band level minus the mean of the bands, ten values), its crest,
// its width. The axes give one; a reference recording gives another (S20,
// « mixer comme ce morceau »). The decision reads a target, never where it
// came from.
struct Target
{
    std::optional<std::array<double, bandCount>> tilt; // nothing: no spectral aim
    std::optional<double> crestDb;
    std::optional<double> sideShare;
    double amount{1.0}; // how far towards it, 0 to 1: the direction's « amount »
    std::string source; // « axes », or the reference's file name

    [[nodiscard]] Value toValue() const;
};

// The spectral tilt of a measure: each band minus the mean of the bands.
[[nodiscard]] std::array<double, bandCount> tiltOf(const StreamMeasure& measure);

// A reference recording, measured like the master, as a target.
[[nodiscard]] Target targetOf(const StreamMeasure& reference, std::string name);

// The axes that move the mix towards a target: more crest than the target
// means more punch wanted down, and so on. `amount` from 0 (the axes given)
// to 1 (all the way to the reference).
[[nodiscard]] Axes towards(const Target& target, const StreamMeasure& master, Axes given, double amount);

// --- the proposal --------------------------------------------------------------

// A measured number a sentence cites. The guards check it against the measure
// it names: a sentence that cites a number the measure does not hold is
// refused like a gain out of bounds.
struct Evidence
{
    std::string measure; // « bands.63 Hz », « lufs », « crest », « overlap.<other>.63 Hz »
    double value{0.0};

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Evidence> fromValue(const Value& value);
};

// One change of one strip, with its sentence. Values are targets, not
// differences: the state reached, which is what makes « Mixer » twice idempotent.
struct Change
{
    enum class Kind
    {
        volume,     // value: dB
        pan,        // value: -1..1
        equaliser,  // parameters: the internal equaliser's, in their units
        compressor, // parameters: the internal compressor's
    };

    TrackId track;
    Kind kind{Kind::volume};
    double value{0.0};
    std::map<std::string, double> parameters;

    std::string sentence; // French, plain, says the measure that justifies it
    std::vector<Evidence> evidence;

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Change> fromValue(const Value& value);
};

struct Proposal
{
    std::vector<Change> changes;
    std::string decidedBy; // « règles », or the model's name

    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<Proposal> fromValue(const Value& value);

    // The changes of one track, in order.
    [[nodiscard]] std::vector<const Change*> of(TrackId track) const;
};

// --- what the decision is given --------------------------------------------------

// Everything the decision reads, and nothing else: numbers, roles, chains,
// axes, target, bounds. Never a sample of audio. This is also, as a Value,
// exactly what a model is sent.
struct Brief
{
    struct Strip
    {
        TrackId track;
        std::string name;
        RoleGuess role;
        double volumeDb{0.0};
        double pan{0.0};
        bool muted{false};
        std::vector<std::string> chain;           // names; « daw.eq », « daw.compressor » for ours
        std::map<std::string, double> equaliser;  // current internal one, if any
        std::map<std::string, double> compressor; // current internal one, if any
        StreamMeasure measure;
    };

    std::vector<Strip> strips;
    StreamMeasure master;
    std::vector<Overlap> overlaps; // indices into strips; never a kick with a bass

    // A kick over a bass, where it hits (S21): what the carving of the bass
    // reads and cites, in place of their overlap, which a decaying kick
    // keeps whatever the bass does (see hitMarginDb).
    struct Margin
    {
        std::size_t hit{0};   // the kick, an index into strips
        std::size_t under{0}; // the bass
        std::size_t band{0};  // the octave that carries the kick, 0 to 2
        double marginDb{0.0};
    };
    std::vector<Margin> margins;
    Axes axes;
    Target target;

    [[nodiscard]] const Strip* find(TrackId track) const;
    [[nodiscard]] Value toValue() const; // what a model reads
};

[[nodiscard]] Brief briefOf(const ProjectState& state,
                            const std::map<std::string, StreamMeasure>& tracks,
                            const StreamMeasure& master,
                            Axes axes,
                            Target target,
                            const tidy::PresetNames& presets = {});

// --- the guards ----------------------------------------------------------------

struct Bounds
{
    static constexpr double cutDb = -6.0;  // an equaliser band, at most this deep
    static constexpr double boostDb = 4.0; // and at most this high
    static constexpr double minQ = 0.3;
    static constexpr double maxQ = 4.0;
    static constexpr double maxRatio = 6.0;
    static constexpr double minAttackMs = 1.0;
    static constexpr double maxMakeupDb = 6.0;
    static constexpr double maxReductionDb = 6.0; // estimated gain reduction
    static constexpr double volumeDownDb = -12.0; // from where the fader is
    static constexpr double volumeUpDb = 6.0;
    static constexpr double floorDb = -60.0; // a fader never goes under: that is muting
    static constexpr double maxPan = 0.8;
    static constexpr double maxCentredPan = 0.1;    // kick, bass, voice stay in the middle
    static constexpr double masterCeilingDb = -1.0; // dBTP, after the mix, rendered

    [[nodiscard]] static double highPassLimit(MixRole role); // Hz
    [[nodiscard]] static Value toValue();
};

struct Refusal
{
    TrackId track;
    std::size_t change{0}; // index in the proposal
    std::string why;       // French, for the model and for the screen

    [[nodiscard]] Value toValue() const;
};

// Every change out of bounds, and every sentence whose evidence the measure
// does not hold. An empty list: the proposal may be tried.
[[nodiscard]] std::vector<Refusal> check(const Brief& brief, const Proposal& proposal);

// The proposal without the refused changes.
[[nodiscard]] Proposal without(const Proposal& proposal, const std::vector<Refusal>& refused);

// --- the rules: a mix without a model ------------------------------------------

// The « mix de base »: deterministic, offline, without a key. Levels by role,
// a high-pass where low energy serves nothing, the kick and the bass carved
// apart where they overlap, the voice given room in the chords, compression
// where the crest is far above what the role wants, pans by role. Every
// change cites the measure it comes from.
[[nodiscard]] Proposal baseMix(const Brief& brief);

// --- from a proposal to the bus ------------------------------------------------

// The commands that reach the proposal from the state: existing verbs only,
// identifiers drawn by `newPluginId` (the caller's, so the bus engenders none).
// An internal equaliser or compressor already in the chain is set, never
// stacked: « Mixer » twice leaves one equaliser per track. Changes on tracks
// the person did not leave audible are skipped.
[[nodiscard]] std::vector<std::unique_ptr<Command>>
compile(const ProjectState& state, const Proposal& proposal, const std::function<PluginId()>& newPluginId);

// The master's fader, lowered so the rendered master stays under the ceiling.
// Nothing when it already does.
[[nodiscard]] std::optional<double> masterTrim(const ProjectState& state, double renderedTruePeakDb);

} // namespace daw::domain::mix
