#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/project/ProjectState.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::domain::tidy
{

// What a track or a playlist line is for, read the way a producer reads a
// project they did not make: the names first — the track's, the preset's, the
// plugin's, the sample's — and the notes when the names say nothing.
//
// Local and deterministic, like the generator: the same project gives the same
// answer offline, in a test, and in a replay. Nothing here is stored: a role
// is a reading of the project, and the project keeps only what the user
// accepts of it — a name, a line.

enum class Family
{
    unknown,
    kick,
    snare,
    clap,
    hat,
    percussion,
    bass,
    chords,
    pad,
    melody,
    fx,
    vocal
};

// Where a guess came from, strongest first. Said to the user, so a
// suggestion can be judged: « d'après le preset « Sub Bass » ».
enum class Evidence
{
    none,
    lineName,
    trackName,
    presetName,
    sampleName,
    pluginName,
    notes
};

struct Guess
{
    Family family{Family::unknown};
    Evidence evidence{Evidence::none};
    double confidence{0.0}; // 0 to 1
    std::string clue;       // the word or the name that decided, for the phrase
};

// What is known of one track.
struct Clues
{
    std::string trackName;
    std::string presetName; // from the engine; empty when the plugin does not tell
    std::string pluginName;
    std::string sampleName;
    bool sampleChannel{false};
    std::vector<Note> notes; // every note the track plays, in every pattern
};

// A name the project was given without anyone choosing it: « Piste 3 »,
// « Track 12 », empty. Such a name says nothing, and may be replaced.
[[nodiscard]] bool isDefaultName(std::string_view name);

// The family a name points to, if one of its words does.
[[nodiscard]] Family familyOfName(std::string_view name);

[[nodiscard]] Guess classify(const Clues& clues);

// The preset a track's instrument has loaded, when the engine knows it.
using PresetNames = std::function<std::string(TrackId)>;

[[nodiscard]] Clues cluesOf(const ProjectState& state, TrackId track, const PresetNames& presets = {});
[[nodiscard]] Guess classifyTrack(const ProjectState& state, TrackId track, const PresetNames& presets = {});

// A line: its name when it has one; else what is filed on it — the tracks of
// its patterns, the samples of its audio clips.
[[nodiscard]] Guess classifyLane(const ProjectState& state, LaneId lane, const PresetNames& presets = {});

// The tracks a line's blocks sound on, most notes first.
[[nodiscard]] std::vector<TrackId> tracksOfLane(const ProjectState& state, LaneId lane);

// The name a producer would give: « Basse », « Accords », « Hi-hat ».
[[nodiscard]] std::string suggestedName(Family family);

// What the generator writes for a family. Nothing for what it cannot write:
// effects, voices, the unknown.
[[nodiscard]] std::optional<generation::Role> generationRole(Family family);

// « d'après le preset « Sub Bass » », in French, for the screen.
[[nodiscard]] std::string because(const Guess& guess);

} // namespace daw::domain::tidy
