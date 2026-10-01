#include "daw/ui/model/ViewFraming.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::framing
{
namespace
{

constexpr double room = 0.05;    // of the width, on each side
constexpr double shortest = 1.0; // beat
constexpr int air = 2;           // semitones over the highest note

} // namespace

std::optional<Span> notes(const std::vector<domain::Note>& all, const std::vector<domain::NoteId>& picked)
{
    std::optional<Span> span;
    for (const auto& note : all)
    {
        if (!picked.empty() && std::find(picked.begin(), picked.end(), note.id) == picked.end())
            continue;
        if (!span.has_value())
            span = Span{note.startBeats, note.startBeats + note.lengthBeats, note.pitch, note.pitch};
        span->from = std::min(span->from, note.startBeats);
        span->to = std::max(span->to, note.startBeats + note.lengthBeats);
        span->low = std::min(span->low, note.pitch);
        span->high = std::max(span->high, note.pitch);
    }
    return span;
}

double beatWidthFor(const Span& span, int pixels, double narrowest, double widest) noexcept
{
    const auto length = std::max(shortest, span.to - span.from);
    const auto width = static_cast<double>(std::max(1, pixels)) / (length * (1.0 + 2.0 * room));
    return std::clamp(width, narrowest, std::max(narrowest, widest));
}

double firstBeatFor(const Span& span, double beatWidth, int pixels) noexcept
{
    const auto shown = static_cast<double>(std::max(1, pixels)) / std::max(beatWidth, 1e-9);
    const auto centre = (span.from + span.to) / 2.0;
    return std::max(0.0, centre - shown / 2.0);
}

int topPitchFor(const Span& span, int rows) noexcept
{
    const auto highest = domain::Note::highestPitch;
    const auto lowestTop = std::min(std::max(rows - 1, 0), highest);
    const auto needed = span.high - span.low + 1;
    const auto top = needed + 2 * air <= rows ? span.high + (rows - needed) / 2 : span.high + air;
    return std::clamp(top, lowestTop, highest);
}

} // namespace daw::ui::framing
