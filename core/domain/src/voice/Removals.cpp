#include "daw/domain/voice/Removals.h"

#include <map>

namespace daw::domain::voice
{

std::vector<std::string> removals(const std::vector<std::string_view>& types)
{
    // Each kind once, in the order they are first met, with how many.
    struct Kind
    {
        const char* one;
        const char* many;
        bool always;
    };
    static const std::map<std::string_view, Kind> kinds{
        {"track.remove", {"retire une piste", "retire % pistes", true}},
        {"pattern.remove", {"retire un pattern", "retire % patterns", true}},
        {"lane.remove", {"retire une ligne", "retire % lignes", true}},
        {"audio.remove", {"retire un clip audio", "retire % clips audio", true}},
        {"plugin.remove", {"retire un plugin", "retire % plugins", true}},
        {"automation.remove_line", {"retire une ligne d'automation", "retire % lignes d'automation", true}},
        {"note.remove", {"retire une note", "retire % notes", false}},
        {"placement.remove", {"retire une pose de pattern", "retire % poses de pattern", false}},
    };

    std::vector<std::string_view> order;
    std::map<std::string_view, int> counts;
    for (const auto type : types)
    {
        if (kinds.find(type) == kinds.end())
            continue;
        if (counts[type]++ == 0)
            order.push_back(type);
    }

    std::vector<std::string> said;
    for (const auto type : order)
    {
        const auto& kind = kinds.at(type);
        const auto count = counts[type];
        if (!kind.always && count < manyRemoved)
            continue;
        if (count == 1)
        {
            said.emplace_back(kind.one);
            continue;
        }
        std::string line{kind.many};
        line.replace(line.find('%'), 1, std::to_string(count));
        said.push_back(std::move(line));
    }
    return said;
}

} // namespace daw::domain::voice
