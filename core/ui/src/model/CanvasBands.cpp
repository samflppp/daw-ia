#include "daw/ui/model/CanvasBands.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <utility>

namespace daw::ui
{
namespace
{

// FNV-1a, 64 bits, as PatternPreviews: a fingerprint, not a defence.
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

// A melodic row with no note yet: an octave from the channel's pitch, room
// to write the first ones.
constexpr int emptyRows = 12;

} // namespace

std::pair<std::size_t, std::size_t> CanvasNotes::within(double fromBeats, double toBeats) const
{
    const auto byStart = [](const domain::Note& note, double beats) { return note.startBeats < beats; };
    const auto first = std::lower_bound(notes.begin(), notes.end(), fromBeats - longestBeats, byStart);
    const auto last = std::lower_bound(first, notes.end(), toBeats, byStart);
    return {static_cast<std::size_t>(first - notes.begin()), static_cast<std::size_t>(last - notes.begin())};
}

std::uint64_t CanvasBands::fingerprint(const domain::Clip& clip) noexcept
{
    // What is drawn: where each note starts, how long it lasts, how high it
    // is, how loud (S19: the colour and the velocity strip), and which note
    // it is, since a picked note is drawn apart.
    auto hash = fnvOffset;
    mix(hash, static_cast<std::uint64_t>(clip.notes.size()));
    for (const auto& note : clip.notes)
    {
        mix(hash, static_cast<std::uint64_t>(note.pitch));
        mix(hash, note.startBeats);
        mix(hash, note.lengthBeats);
        mix(hash, static_cast<std::uint64_t>(note.velocity));
        mix(hash, std::hash<std::string>{}(note.id.toString()));
    }
    return hash;
}

std::size_t CanvasBands::refresh(const domain::ProjectState& state)
{
    // The rows: kept when their fingerprint did not move, sorted again when
    // it did, dropped with their pattern or their track.
    std::size_t built = 0;
    std::vector<CanvasNotes> rows;
    for (const auto& pattern : state.patterns())
    {
        for (const auto& clip : pattern.clips)
        {
            const auto print = fingerprint(clip);
            const auto kept = std::find_if(rows_.begin(),
                                           rows_.end(),
                                           [&](const CanvasNotes& row)
                                           {
                                               return row.pattern == pattern.id &&
                                                      row.track == clip.trackId && row.clip == clip.id &&
                                                      row.fingerprint == print;
                                           });
            if (kept != rows_.end())
            {
                rows.push_back(std::move(*kept));
                continue;
            }

            CanvasNotes row{};
            row.pattern = pattern.id;
            row.track = clip.trackId;
            row.clip = clip.id;
            row.fingerprint = print;
            row.notes = clip.notes;
            std::stable_sort(row.notes.begin(),
                             row.notes.end(),
                             [](const domain::Note& a, const domain::Note& b)
                             { return a.startBeats < b.startBeats; });
            for (const auto& note : row.notes)
            {
                row.longestBeats = std::max(row.longestBeats, note.lengthBeats);
                row.lowest = std::min(row.lowest, note.pitch);
                row.highest = std::max(row.highest, note.pitch);
            }
            rows.push_back(std::move(row));
            ++built;
        }
    }
    rows_ = std::move(rows);
    builds_ += built;

    // The bands: per line, the tracks its blocks play, in rack order, over
    // the range their notes span. Cheap next to the rows: a line reads the
    // extremes of each row, not its notes.
    lines_.clear();
    for (const auto& lane : state.lanes())
    {
        std::vector<domain::PatternId> laid;
        for (const auto& placement : state.arrangement())
        {
            if (placement.laneId == lane.id &&
                std::find(laid.begin(), laid.end(), placement.patternId) == laid.end())
                laid.push_back(placement.patternId);
        }
        if (laid.empty())
            continue;

        Line line{};
        line.lane = lane.id;
        for (const auto& track : state.tracks())
        {
            auto present = false;
            auto lowest = domain::Note::highestPitch;
            auto highest = domain::Note::lowestPitch;
            for (const auto& row : rows_)
            {
                if (row.track != track.id || std::find(laid.begin(), laid.end(), row.pattern) == laid.end())
                    continue;
                present = true;
                lowest = std::min(lowest, row.lowest);
                highest = std::max(highest, row.highest);
            }
            if (!present)
                continue;

            CanvasBand band{};
            band.track = track.id;
            const auto sampler = track.sample.has_value();
            if (lowest > highest)
            {
                // Nothing written yet: the channel's pitch, and an octave
                // above it for an instrument.
                band.low = track.channelPitch;
                band.high = sampler ? track.channelPitch : track.channelPitch + emptyRows - 1;
            }
            else if (sampler)
            {
                band.low = lowest;
                band.high = highest;
            }
            else
            {
                band.low = lowest - canvas::roomSemitones;
                band.high = highest + canvas::roomSemitones;
            }
            band.low = std::clamp(band.low, domain::Note::lowestPitch, domain::Note::highestPitch);
            band.high = std::clamp(band.high, band.low, domain::Note::highestPitch);
            line.bands.push_back(band);
        }
        lines_.push_back(std::move(line));
    }
    return built;
}

const std::vector<CanvasBand>& CanvasBands::of(domain::LaneId lane) const noexcept
{
    static const std::vector<CanvasBand> none;
    const auto found =
        std::find_if(lines_.begin(), lines_.end(), [lane](const Line& line) { return line.lane == lane; });
    return found != lines_.end() ? found->bands : none;
}

const CanvasNotes* CanvasBands::notes(domain::PatternId pattern, domain::TrackId track) const noexcept
{
    const auto found =
        std::find_if(rows_.begin(),
                     rows_.end(),
                     [&](const CanvasNotes& row) { return row.pattern == pattern && row.track == track; });
    return found != rows_.end() ? &*found : nullptr;
}

namespace canvas
{

CanvasBand extended(CanvasBand band, CanvasExtension extension) noexcept
{
    band.low = std::clamp(band.low - extension.below, domain::Note::lowestPitch, band.low);
    band.high = std::clamp(band.high + extension.above, band.high, domain::Note::highestPitch);
    return band;
}

double rowHeight(double beatWidth, const Scale& scale) noexcept
{
    const auto sixteenth = beatWidth / 4.0;
    return std::clamp(sixteenth * scale.grabRow / scale.grabStep, 0.0, scale.maxRow);
}

double approach(double rowPixels, const Scale& scale) noexcept
{
    if (scale.grabRow <= scale.approachRow)
        return rowPixels >= scale.grabRow ? 1.0 : 0.0;
    return std::clamp((rowPixels - scale.approachRow) / (scale.grabRow - scale.approachRow), 0.0, 1.0);
}

bool grabbable(double beatWidth, double rowPixels, const Scale& scale) noexcept
{
    return rowPixels >= scale.grabRow && beatWidth / 4.0 >= scale.grabStep;
}

int rowCount(const std::vector<CanvasBand>& bands) noexcept
{
    int rows = 0;
    for (const auto& band : bands)
        rows += band.rows();
    return rows;
}

int lineHeight(int rows, double rowPixels, int chrome, int minimum) noexcept
{
    if (rows <= 0)
        return minimum;
    const auto needed = chrome + static_cast<int>(std::ceil(static_cast<double>(rows) * rowPixels));
    return std::max(minimum, needed);
}

double fittedRow(int rows, double rowPixels, int chrome, int height) noexcept
{
    if (rows <= 0)
        return rowPixels;
    return std::max(rowPixels, static_cast<double>(height - chrome) / static_cast<double>(rows));
}

} // namespace canvas

} // namespace daw::ui
