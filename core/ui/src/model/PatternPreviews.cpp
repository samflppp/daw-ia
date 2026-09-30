#include "daw/ui/model/PatternPreviews.h"

#include <algorithm>
#include <cstring>

namespace daw::ui
{
namespace
{

// FNV-1a, 64 bits: a fingerprint, not a defence against anyone.
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

void mix(std::uint64_t& hash, std::uint64_t value) noexcept
{
    for (int byte = 0; byte < 8; ++byte)
    {
        hash ^= (value >> (byte * 8)) & 0xFFU;
        hash *= fnvPrime;
    }
}

void mix(std::uint64_t& hash, double value) noexcept
{
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof value);
    std::memcpy(&bits, &value, sizeof bits);
    mix(hash, bits);
}

} // namespace

std::uint64_t PatternPreviews::fingerprint(const domain::Pattern& pattern) noexcept
{
    // What the picture depends on, and nothing else: the length it is drawn
    // against, and where each note starts, how long it lasts, how high it is.
    // Velocity is not drawn, so it does not count.
    auto hash = fnvOffset;
    mix(hash, pattern.lengthBeats);
    for (const auto& row : pattern.clips)
    {
        mix(hash, static_cast<std::uint64_t>(row.notes.size()));
        for (const auto& note : row.notes)
        {
            mix(hash, static_cast<std::uint64_t>(note.pitch));
            mix(hash, note.startBeats);
            mix(hash, note.lengthBeats);
        }
    }
    return hash;
}

PatternPreview PatternPreviews::build(const domain::Pattern& pattern)
{
    PatternPreview preview;
    if (pattern.lengthBeats <= 0.0)
        return preview;

    int lowest = domain::Note::highestPitch;
    int highest = domain::Note::lowestPitch;
    for (const auto& row : pattern.clips)
    {
        for (const auto& note : row.notes)
        {
            lowest = std::min(lowest, note.pitch);
            highest = std::max(highest, note.pitch);
        }
    }
    if (lowest > highest)
        return preview; // no note: an empty picture, not a picture of nothing

    // One slot per semitone between the lowest and the highest note: a bass
    // line fills the height, and a drum pattern on one pitch is one line.
    const auto slots = static_cast<float>(highest - lowest + 1);
    const auto length = static_cast<float>(pattern.lengthBeats);

    for (const auto& row : pattern.clips)
    {
        for (const auto& note : row.notes)
        {
            const auto start = static_cast<float>(note.startBeats) / length;
            if (start >= 1.0f)
                continue; // past the end of the pattern: never heard, never drawn

            PreviewNote drawn{};
            drawn.x = std::max(0.0f, start);
            drawn.width = std::min(1.0f, start + static_cast<float>(note.lengthBeats) / length) - drawn.x;
            drawn.y = static_cast<float>(highest - note.pitch) / slots;
            drawn.height = 1.0f / slots;
            preview.notes.push_back(drawn);
        }
    }

    return preview;
}

std::size_t PatternPreviews::refresh(const domain::ProjectState& state)
{
    std::size_t built = 0;
    rebuilt_.clear();
    std::vector<Entry> kept;
    kept.reserve(state.patterns().size());

    for (const auto& pattern : state.patterns())
    {
        const auto print = fingerprint(pattern);
        const auto found = std::find_if(entries_.begin(),
                                        entries_.end(),
                                        [&pattern](const Entry& entry) { return entry.id == pattern.id; });

        if (found != entries_.end() && found->fingerprint == print)
        {
            kept.push_back(std::move(*found));
            continue;
        }

        kept.push_back(Entry{pattern.id, print, build(pattern)});
        rebuilt_.push_back(pattern.id);
        ++built;
    }

    entries_ = std::move(kept);
    builds_ += built;
    return built;
}

const PatternPreview* PatternPreviews::find(domain::PatternId id) const noexcept
{
    const auto found =
        std::find_if(entries_.begin(), entries_.end(), [id](const Entry& entry) { return entry.id == id; });
    return found != entries_.end() ? &found->preview : nullptr;
}

} // namespace daw::ui
