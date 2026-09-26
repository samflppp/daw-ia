#pragma once

#include <string>
#include <vector>

namespace daw::domain::copilot
{

// What a copilot needs to mix, and whether it has it.
//
// Not an AI and not a feature: a checklist. Each need names the verbs or the
// measures that would satisfy it — command types of the registry, or methods
// the application answers over JSON-RPC — and a need is met when any one of
// them is offered. Written down here, once, so that the answer to "can the
// copilot mix yet?" is a list anyone can read and a test pins, not a feeling.
//
// The list says what a mixing engineer does: set levels and places, group,
// share an effect, shape the sound with effects whose controls it
// understands, and judge — by measuring over the passage it is working on,
// not over the last 300 ms, in the units mastering speaks.
struct MixingNeed
{
    std::string what;                     // in French: it ends up in the review
    std::vector<std::string> satisfiedBy; // any one of them is enough
    std::string why;                      // in French too
};

[[nodiscard]] const std::vector<MixingNeed>& mixingNeeds();

// The needs that none of `offered` meets, in the order of mixingNeeds().
[[nodiscard]] std::vector<MixingNeed> mixingGaps(const std::vector<std::string>& offered);

} // namespace daw::domain::copilot
