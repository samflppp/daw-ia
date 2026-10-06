#include "daw/domain/live/Inputs.h"

#include <algorithm>

namespace daw::domain::live
{

InputChange follow(const std::vector<InputSlot>& listened,
                   const std::vector<std::string>& present,
                   int firstSource,
                   int count)
{
    InputChange change;
    for (const auto& slot : listened)
    {
        const auto still = std::find(present.begin(), present.end(), slot.identifier) != present.end();
        (still ? change.kept : change.removed).push_back(slot);
    }

    std::vector<bool> taken(static_cast<std::size_t>(std::max(0, count)), false);
    for (const auto& slot : change.kept)
    {
        const auto index = slot.source - firstSource;
        if (index >= 0 && index < count)
            taken[static_cast<std::size_t>(index)] = true;
    }

    for (const auto& identifier : present)
    {
        const auto known =
            std::any_of(change.kept.begin(),
                        change.kept.end(),
                        [&identifier](const InputSlot& slot) { return slot.identifier == identifier; });
        if (known)
            continue;
        const auto free = std::find(taken.begin(), taken.end(), false);
        if (free == taken.end())
        {
            change.waiting.push_back(identifier);
            continue;
        }
        *free = true;
        change.added.push_back(
            {identifier, firstSource + static_cast<int>(std::distance(taken.begin(), free))});
    }
    return change;
}

} // namespace daw::domain::live
