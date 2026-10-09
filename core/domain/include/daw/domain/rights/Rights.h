#pragma once

#include <string>

namespace daw::domain::rights
{

// « Ai-je le droit ? » — the one place a feature asks it (S26, decided on
// 9 October 2026, the « Flex-Model » licences of docs/contexte-projet.md).
//
// Two routes, two ways to pay later: what goes through the API (a
// subscription or credits), and what runs on the machine (bought once). The
// prototype bills nothing: every answer is yes. Each feature asks once, where
// it starts; on a no, it says refusal() and changes nothing.
enum class Feature
{
    copilot,    // the copilot answering a request
    mixByModel, // the mix decided by the model
    mix,        // the mix by the rules
    generation, // notes generated on the machine
    stems,      // a clip separated into stems
    voice,      // the push-to-talk
    kit,        // a kit chosen from the person's samples
    buses,      // the smart buses
    direction,  // the direction read from references
};

enum class Route
{
    api,   // through Anthropic's API: a subscription or credits
    local, // on this machine: bought once
};

[[nodiscard]] Route routeOf(Feature feature) noexcept;

// Yes, for the prototype.
[[nodiscard]] bool allows(Feature feature) noexcept;

// What a feature says when it is refused, in French, one line.
[[nodiscard]] std::string refusal(Feature feature);

// For a check only (--verify-droits): every answer becomes no, then yes again.
void refuseAllForCheck(bool refuse) noexcept;

} // namespace daw::domain::rights
