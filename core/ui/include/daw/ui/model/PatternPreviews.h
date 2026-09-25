#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/project/ProjectState.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace daw::ui
{

// What a block of the playlist draws of its pattern: every note of every row,
// as a rectangle in a unit square. Across, the note's place in the pattern's
// length; down, its pitch within the pattern's own range, highest on top.
//
// FL's picture, and the reason the playlist stops being a row of coloured
// boxes: a pattern laid eight times shows eight times what it holds.
struct PreviewNote
{
    float x{0.0f};
    float width{0.0f};
    float y{0.0f};
    float height{0.0f};

    friend bool operator==(const PreviewNote&, const PreviewNote&) = default;
};

struct PatternPreview
{
    std::vector<PreviewNote> notes;
};

// The previews of every pattern, kept between repaints.
//
// Computed once per pattern and kept. refresh() is called when the project
// changed, never at paint time, and it rebuilds a preview only when what it
// draws changed: the notes of the pattern, or its length. A placement moved,
// a fader dragged, a pattern laid once more — none of them rebuilds anything,
// because none of them changes what a pattern holds. Deciding costs a pass
// over the notes to fingerprint them; it is the drawing that is not redone.
//
// Pure C++, no JUCE: the part of the playlist with a right answer, so the part
// a test pins down.
class PatternPreviews
{
public:
    // Brings the cache in line with the state. Returns how many previews it
    // built; a pattern removed takes its preview with it.
    std::size_t refresh(const domain::ProjectState& state);

    [[nodiscard]] const PatternPreview* find(domain::PatternId id) const noexcept;

    // Every preview built since this cache was made. Read by the tests and the
    // verification, which have to prove what was not rebuilt.
    [[nodiscard]] std::size_t builds() const noexcept { return builds_; }

    [[nodiscard]] static PatternPreview build(const domain::Pattern& pattern);
    [[nodiscard]] static std::uint64_t fingerprint(const domain::Pattern& pattern) noexcept;

private:
    struct Entry
    {
        domain::PatternId id{};
        std::uint64_t fingerprint{0};
        PatternPreview preview;
    };

    std::vector<Entry> entries_;
    std::size_t builds_{0};
};

} // namespace daw::ui
