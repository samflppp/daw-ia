#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace daw::domain::voice
{

// What a phrase spoken removes or overwrites (S25, decided on 7 October
// 2026): before the copilot's commands for a phrase said aloud are written,
// those that take away a track, a pattern, a lane, an audio clip, a plugin or
// an automation line — or a run of notes or placements, which is how a
// pattern is emptied — are named, and the person confirms. The same phrase
// typed is not asked again: the keyboard does not mishear.
//
// `types` are the command types the copilot's request expands to, in order.
// Nothing is returned when nothing is removed; else one French line per kind.
[[nodiscard]] std::vector<std::string> removals(const std::vector<std::string_view>& types);

// Notes or placements removed by one phrase, from which it is a removal.
inline constexpr int manyRemoved = 4;

} // namespace daw::domain::voice
