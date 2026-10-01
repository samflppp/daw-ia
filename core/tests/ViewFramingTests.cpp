#include "daw/ui/model/ViewFraming.h"

#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
namespace framing = daw::ui::framing;

namespace
{

Note at(int pitch, double start, double length)
{
    Note note{};
    note.id = NoteId::generate();
    note.pitch = pitch;
    note.startBeats = start;
    note.lengthBeats = length;
    return note;
}

} // namespace

TEST_CASE("F frames the picked notes, or every note when none is picked")
{
    const std::vector<Note> all{at(60, 2.0, 1.0), at(67, 4.0, 0.5), at(72, 12.0, 2.0)};

    const auto everything = framing::notes(all, {});
    REQUIRE(everything.has_value());
    CHECK(*everything == framing::Span{2.0, 14.0, 60, 72});

    const auto two = framing::notes(all, {all[0].id, all[1].id});
    REQUIRE(two.has_value());
    CHECK(*two == framing::Span{2.0, 4.5, 60, 67});

    CHECK_FALSE(framing::notes({}, {}).has_value());
}

TEST_CASE("the framed notes fill the width, with room, and are centred")
{
    const framing::Span span{4.0, 8.0, 60, 64};

    // Four beats and a tenth of room across 1100 pixels: 250 a beat.
    const auto width = framing::beatWidthFor(span, 1100, 6.0, 400.0);
    CHECK(width == doctest::Approx(250.0));
    CHECK(framing::firstBeatFor(span, width, 1100) == doctest::Approx(3.8));

    // Never past the widest the panel draws, never under the narrowest.
    CHECK(framing::beatWidthFor(span, 1100, 6.0, 192.0) == doctest::Approx(192.0));
    CHECK(framing::beatWidthFor(framing::Span{0.0, 4000.0, 60, 60}, 1100, 6.0, 192.0) ==
          doctest::Approx(6.0));

    // A single short note still frames a beat, not a pixel-wide sliver.
    CHECK(framing::beatWidthFor(framing::Span{2.0, 2.25, 60, 60}, 1100, 6.0, 4000.0) ==
          doctest::Approx(1000.0));

    // Never before the start of the pattern.
    CHECK(framing::firstBeatFor(framing::Span{0.0, 1.0, 60, 60}, 100.0, 1100) == doctest::Approx(0.0));
}

TEST_CASE("eight notes in a window of twenty-eight keys are centred, not lost among 128")
{
    // C4 to G4 in a window of 28 keys: the eight in the middle.
    const framing::Span span{0.0, 4.0, 60, 67};
    const auto top = framing::topPitchFor(span, 28);
    CHECK(top == 67 + (28 - 8) / 2);
    CHECK(top - 27 <= 60);

    // Too tall for the window: the highest note under two semitones of air.
    CHECK(framing::topPitchFor(framing::Span{0.0, 4.0, 24, 96}, 28) == 98);

    // Within MIDI.
    CHECK(framing::topPitchFor(framing::Span{0.0, 4.0, 120, 127}, 28) == 127);
    CHECK(framing::topPitchFor(framing::Span{0.0, 4.0, 0, 3}, 28) == 27);
}
