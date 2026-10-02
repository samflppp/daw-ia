#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace daw::ui
{

// The canvas (S18): the playlist and the piano roll as one surface, where the
// zoom crosses from blocks on their lines to notes that can be grabbed.
//
// A line of the canvas unfolds into bands, one per track that plays in a
// pattern laid on it, in the order of the channel rack. A band is a small
// piano roll framed on what its track plays there: its lowest and highest
// notes, two semitones of room either side, one row for a sampler channel
// that plays one pitch. A block draws, in each band, the notes of its own
// pattern for that track.
//
// This is the part with a right answer: which bands a line has, what range
// each covers, how tall a row is at a zoom, when a note can be grabbed. Pure
// C++, no JUCE, so a test pins it without a window.

struct CanvasBand
{
    domain::TrackId track{};
    int low{60}; // both included
    int high{60};

    [[nodiscard]] int rows() const noexcept { return high - low + 1; }

    friend bool operator==(const CanvasBand&, const CanvasBand&) = default;
};

// Rows added by hand to a band, above and below what its notes span: the
// room to write a note nowhere near the others. It is the screen's, like the
// zoom, and never the project's.
struct CanvasExtension
{
    int above{0};
    int below{0};

    friend bool operator==(const CanvasExtension&, const CanvasExtension&) = default;
};

// The notes of one track in one pattern, sorted by start, and what is needed
// to draw only those in sight.
struct CanvasNotes
{
    domain::PatternId pattern{};
    domain::TrackId track{};
    domain::ClipId clip{};
    std::vector<domain::Note> notes; // by start
    double longestBeats{0.0};
    int lowest{domain::Note::highestPitch}; // no note: lowest > highest
    int highest{domain::Note::lowestPitch};
    std::uint64_t fingerprint{0};

    // The notes that may cross the window [from, to), in pattern beats: a
    // dichotomy on the start, widened by the longest note.
    [[nodiscard]] std::pair<std::size_t, std::size_t> within(double fromBeats, double toBeats) const;
};

// The bands of every line, and the sorted notes of every row of every pattern,
// kept between repaints. Same rule as PatternPreviews (S11): refresh() when the
// project changed, never at paint time and never on a move of the view; a
// row's notes are sorted again only when they changed.
class CanvasBands
{
public:
    // Returns how many rows it sorted again.
    std::size_t refresh(const domain::ProjectState& state);

    // The bands of a line, top to bottom. Empty for a line that holds no
    // pattern block.
    [[nodiscard]] const std::vector<CanvasBand>& of(domain::LaneId lane) const noexcept;

    [[nodiscard]] const CanvasNotes* notes(domain::PatternId pattern, domain::TrackId track) const noexcept;

    // The pattern mode (S19): one pattern alone, a band for every channel of
    // the rack, in its order — those the pattern has no row for too, framed
    // as a row with no note yet. Writing in one of them opens its row.
    [[nodiscard]] std::vector<CanvasBand> ofPattern(const domain::ProjectState& state,
                                                    domain::PatternId pattern) const;

    // Every row sorted since this cache was made. Read by the tests and the
    // verification, which prove what was not redone.
    [[nodiscard]] std::size_t builds() const noexcept { return builds_; }

    [[nodiscard]] static std::uint64_t fingerprint(const domain::Clip& clip) noexcept;

private:
    struct Line
    {
        domain::LaneId lane{};
        std::vector<CanvasBand> bands;
    };

    std::vector<CanvasNotes> rows_;
    std::vector<Line> lines_;
    std::size_t builds_{0};
};

namespace canvas
{

// Two semitones of room over the highest note and under the lowest: where the
// next note of a line most often goes.
inline constexpr int roomSemitones = 2;

// A band's range once the rows added by hand are counted, kept in MIDI.
[[nodiscard]] CanvasBand extended(CanvasBand band, CanvasExtension extension) noexcept;

// The scale, from the tokens: a row reaches `grabRow` pixels when a
// sixteenth reaches `grabStep`, so the two axes grow together and cross the
// threshold at the same zoom. `maxRow` is the piano roll's key height.
// `approachRow` is where the grid starts to show.
struct Scale
{
    double grabRow{7.0};
    double grabStep{8.0};
    double maxRow{14.0};
    double approachRow{3.5};
};

// The height of a row at a zoom, in pixels, before a line is stretched to its
// minimum height.
[[nodiscard]] double rowHeight(double beatWidth, const Scale& scale) noexcept;

// How far the view is between the scale of blocks and the scale of notes,
// from 0 to 1: the grid and the note colour fade in with it, so the change is
// seen before it is reached.
[[nodiscard]] double approach(double rowPixels, const Scale& scale) noexcept;

// Whether a note can be grabbed: a row and a sixteenth big enough to aim at.
[[nodiscard]] bool grabbable(double beatWidth, double rowPixels, const Scale& scale) noexcept;

// The rows of a set of bands, and the height their line takes: its chrome
// (label, insets) and its rows, never under the playlist's line height.
[[nodiscard]] int rowCount(const std::vector<CanvasBand>& bands) noexcept;
[[nodiscard]] int lineHeight(int rows, double rowPixels, int chrome, int minimum) noexcept;

// The row height a line of that height actually draws: stretched when the
// minimum height is taller than the rows need.
[[nodiscard]] double fittedRow(int rows, double rowPixels, int chrome, int height) noexcept;

} // namespace canvas

} // namespace daw::ui
