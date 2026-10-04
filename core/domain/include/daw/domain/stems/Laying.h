#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/command/Command.h"
#include "daw/domain/project/ProjectState.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace daw::domain::stems
{

// Laying the stems of a separation in the project (S22).
//
// A separation gives four files: voice, drums, bass, the rest. Each becomes a
// track of its own, named after its stem and the source, with the mixing role
// its stem implies, and one audio clip at the beat the source started on. A
// source that was a clip of the project is removed with them: the stems add
// up to it. Everything is one list of existing commands — track.add,
// track.set_role, audio.place, audio.remove —, executed as one group: one
// Ctrl+Z takes it all back. No new verb.
//
// Pure: the caller engenders every identifier before, as every command asks.

// The stems, in the order their tracks are laid: the names the separator
// writes (services/src/daw_services/stems).
inline constexpr std::array<std::string_view, 4> names{"vocals", "drums", "bass", "other"};

// "Voix", "Batterie", "Basse", "Le reste".
[[nodiscard]] std::string_view label(std::string_view stem) noexcept;

// The mixing role a stem is: vocal, percussion, bass. The rest has none: the
// mix reads it from the sound, as it does for any track whose role was not
// chosen.
[[nodiscard]] std::optional<MixRole> roleOf(std::string_view stem) noexcept;

struct Stem
{
    std::string name; // one of `names`
    SampleRef sample; // already in the content store
    TrackId track;    // engendered by the caller
    AudioClipId clip; // engendered by the caller
};

struct Laying
{
    std::string sourceName; // "Ma chanson.wav", for the tracks' names
    double startBeats{0.0};
    std::optional<AudioClipId> replaced; // the source, when it was a clip
    std::vector<Stem> stems;
};

// The commands that lay `laying` in `state`, in order. Fails, touching
// nothing, on a stem of an unknown name, a stem given twice, an identifier
// already in the project, or a replaced clip that is not there.
[[nodiscard]] Result<std::vector<std::unique_ptr<Command>>> commandsFor(const ProjectState& state,
                                                                        const Laying& laying);

// "Séparer en stems : Ma chanson.wav", the history's label for the group.
[[nodiscard]] std::string groupLabel(const Laying& laying);

} // namespace daw::domain::stems
