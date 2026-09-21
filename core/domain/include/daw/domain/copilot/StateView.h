#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <vector>

namespace daw::domain::copilot
{

// What the copilot is shown of the project.
//
// The problem is not the format — ProjectState already serializes itself — it
// is the size. A whole state carries every note of every clip and every
// parameter of every plugin, which on a real beat is tens of thousands of
// tokens per request, paid again at every turn.
//
// So the view has two levels, and the split follows what the model has to
// decide rather than what is cheap to write:
//
//   the summary, sent at every request. Tempo, tracks, their volume, pan,
//   mute, their plugins, their clips — and for each clip, how many notes it
//   holds and the lowest and highest pitch in it. That is enough to choose a
//   track, a clip or a plugin, which is what almost every request needs.
//
//   the notes, asked for one clip at a time. "Quantize the notes of the clip"
//   needs the identifier of every note, because note.quantize names them one
//   by one — and it names them one by one on purpose: a payload meaning "all
//   the notes" would quantize different notes when replayed on a clip that has
//   grown since.
//
// Two things that are not project state are in here too, because a copilot
// that cannot see them works blind: the transport, and the plugins installed
// on this machine. Neither is journalled, both are read the same way.
struct MachinePlugins
{
    // What the machine holds, as the domain names a plugin. The application
    // fills this from the catalogue; the domain never scans anything.
    std::vector<PluginRef> available;

    // The list is cut at this many entries. A machine with three hundred
    // plugins would spend more tokens on its catalogue than on the project,
    // and the copilot has a search tool for what the cut leaves out.
    static constexpr std::size_t maxListed = 60;
};

// The summary. Never the notes, never the plugin parameters.
[[nodiscard]] Value summarise(const ProjectState& state, const MachinePlugins& plugins);

// The notes of one clip, with their identifiers, asked for when they are
// needed and not before.
[[nodiscard]] Result<Value> clipNotes(const ProjectState& state, ClipId clipId);

// The installed plugins whose name holds `query`, ignoring case. What the cut
// in the summary leaves out is reachable this way, and only this way.
[[nodiscard]] Value findPlugins(const MachinePlugins& plugins, std::string_view query);

} // namespace daw::domain::copilot
