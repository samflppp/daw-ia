#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/generation/Transform.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;

// The proof of S16 §5 is measured on the notes, before and after: the rhythm
// kept when it has to be, the pitches kept when they have to be, and nothing
// out of the key that the generator wrote. The thresholds were written before
// the first run.

namespace
{

Note note(int pitch, double start, double length)
{
    Note out{};
    out.id = NoteId::generate();
    out.pitch = pitch;
    out.velocity = 96;
    out.startBeats = start;
    out.lengthBeats = length;
    return out;
}

// Four bars: a keys row playing Am | F | G | Am, and the melody a person wrote
// on the Lead, in A minor.
struct Written
{
    explicit Written(std::vector<Note> melody)
    {
        Track leadTrack{};
        leadTrack.id = lead;
        leadTrack.name = "Lead";
        REQUIRE(state.addTrack(leadTrack).ok());
        Track keysTrack{};
        keysTrack.id = TrackId::generate();
        keysTrack.name = "Keys";
        REQUIRE(state.addTrack(keysTrack).ok());

        Pattern pattern{};
        pattern.id = patternId;
        pattern.lengthBeats = 16.0;

        Clip keysRow{};
        keysRow.id = ClipId::generate();
        keysRow.trackId = keysTrack.id;
        const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 59, 62}, {57, 60, 64}};
        for (int bar = 0; bar < 4; ++bar)
        {
            for (const auto pitch : chords[bar])
                keysRow.notes.push_back(note(pitch, bar * 4.0, 4.0));
        }
        pattern.clips.push_back(keysRow);

        Clip leadRow{};
        leadRow.id = ClipId::generate();
        leadRow.trackId = lead;
        leadRow.notes = std::move(melody);
        pattern.clips.push_back(leadRow);

        REQUIRE(state.addPattern(pattern).ok());

        auto built = Context::of(state, patternId, lead, 0.0, 16.0);
        REQUIRE(built.ok());
        context = std::move(built).value();
        constraints = resolve({}, context);
        source = sourceOf(context);
    }

    [[nodiscard]] std::vector<GhostNote> run(Transform transform, int variant = 0) const
    {
        return generation::transform(transform, source, context, constraints, model, variant);
    }

    ProjectState state;
    TrackId lead{TrackId::generate()};
    PatternId patternId{PatternId::generate()};
    Context context;
    ResolvedConstraints constraints;
    std::vector<GhostNote> source;
    StyleModel model{StyleModel::fallback()};
};

std::vector<Note> aMinorMelody()
{
    return {note(69, 0.0, 1.0),
            note(72, 1.0, 0.5),
            note(71, 1.5, 0.5),
            note(69, 2.0, 2.0),
            note(65, 4.0, 1.0),
            note(69, 5.0, 1.0),
            note(72, 6.0, 2.0),
            note(67, 8.0, 0.5),
            note(71, 8.5, 0.5),
            note(74, 9.0, 1.0),
            note(71, 10.0, 2.0),
            note(69, 12.0, 1.0),
            note(76, 13.0, 1.0),
            note(69, 14.0, 2.0)};
}

using Rhythm = std::vector<std::tuple<long long, long long, int>>; // start, length (1/960), velocity

Rhythm rhythmOf(const std::vector<GhostNote>& notes)
{
    Rhythm out;
    for (const auto& ghost : notes)
        out.emplace_back(
            std::llround(ghost.startBeats * 960.0), std::llround(ghost.lengthBeats * 960.0), ghost.velocity);
    std::sort(out.begin(), out.end());
    return out;
}

std::set<long long> onsetsOf(const std::vector<GhostNote>& notes)
{
    std::set<long long> out;
    for (const auto& ghost : notes)
        out.insert(std::llround(ghost.startBeats * 960.0));
    return out;
}

// The pitches in the order they are played.
std::vector<int> pitchesOf(std::vector<GhostNote> notes)
{
    std::stable_sort(notes.begin(),
                     notes.end(),
                     [](const GhostNote& lhs, const GhostNote& rhs)
                     { return lhs.startBeats < rhs.startBeats; });
    std::vector<int> out;
    for (const auto& ghost : notes)
        out.push_back(ghost.pitch);
    return out;
}

double meanPitch(const std::vector<GhostNote>& notes)
{
    return std::accumulate(notes.begin(),
                           notes.end(),
                           0.0,
                           [](double sum, const GhostNote& g) { return sum + g.pitch; }) /
           static_cast<double>(notes.size());
}

bool allIn(const std::vector<GhostNote>& notes, Key key)
{
    return std::all_of(
        notes.begin(), notes.end(), [key](const GhostNote& g) { return inScale(g.pitch, key); });
}

const Key aMinor{9, Mode::minor};

} // namespace

TEST_CASE("transform: the source is the notes of the zone, in time order")
{
    const Written written{aMinorMelody()};
    CHECK(written.source.size() == 14);
    CHECK(written.constraints.key.value == aMinor);
}

TEST_CASE("transform: keep the rhythm — same attacks, lengths and velocities, other pitches, all in the key")
{
    const Written written{aMinorMelody()};
    for (int variant = 0; variant < 4; ++variant)
    {
        const auto after = written.run(Transform::keepRhythm, variant);
        REQUIRE(after.size() == written.source.size());
        CHECK(rhythmOf(after) == rhythmOf(written.source));

        const auto before = pitchesOf(written.source);
        const auto now = pitchesOf(after);
        std::size_t changed = 0;
        for (std::size_t index = 0; index < now.size(); ++index)
            changed += now[index] != before[index] ? 1U : 0U;
        MESSAGE("variant " << variant << ": " << changed << " of " << now.size() << " pitches changed");
        CHECK(changed * 2 >= now.size()); // half the pitches at least
        CHECK(allIn(after, aMinor));
    }
}

TEST_CASE("transform: keep the pitches — the same pitches in the same order, other attacks")
{
    const Written written{aMinorMelody()};
    for (int variant = 0; variant < 4; ++variant)
    {
        const auto after = written.run(Transform::keepPitches, variant);
        REQUIRE(after.size() == written.source.size());
        CHECK(pitchesOf(after) == pitchesOf(written.source));
        CHECK(onsetsOf(after) != onsetsOf(written.source));
        for (const auto& ghost : after)
        {
            CHECK(ghost.startBeats >= 0.0);
            CHECK(ghost.startBeats + ghost.lengthBeats <= 16.0 + 1e-9);
        }
    }
}

TEST_CASE("transform: a light variation changes one note, and one only")
{
    const Written written{aMinorMelody()};
    std::set<std::string> kinds;
    for (int variant = 0; variant < 12; ++variant)
    {
        const auto after = written.run(Transform::variation, variant);
        REQUIRE(after.size() == written.source.size());

        // As multisets: one note left, one note came.
        std::vector<std::tuple<long long, long long, int, int>> left;
        std::vector<std::tuple<long long, long long, int, int>> came;
        const auto full = [](const std::vector<GhostNote>& notes)
        {
            std::vector<std::tuple<long long, long long, int, int>> out;
            for (const auto& g : notes)
                out.emplace_back(std::llround(g.startBeats * 960.0),
                                 std::llround(g.lengthBeats * 960.0),
                                 g.velocity,
                                 g.pitch);
            std::sort(out.begin(), out.end());
            return out;
        };
        const auto a = full(written.source);
        const auto b = full(after);
        std::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(left));
        std::set_difference(b.begin(), b.end(), a.begin(), a.end(), std::back_inserter(came));
        CHECK(left.size() == 1);
        CHECK(came.size() == 1);
        if (left.size() == 1 && came.size() == 1)
        {
            if (std::get<0>(left[0]) != std::get<0>(came[0]))
                kinds.insert("déplacée");
            else if (std::get<2>(left[0]) != std::get<2>(came[0]))
                kinds.insert("vélocité");
            else if (std::get<3>(left[0]) != std::get<3>(came[0]))
            {
                kinds.insert("fin");
                CHECK(inScale(std::get<3>(came[0]), aMinor));
            }
        }
    }
    // The three kinds are drawn, not always the same one.
    CHECK(kinds.size() == 3);
}

TEST_CASE("transform: darker — same rhythm, lower, still in the key, a major line goes to its minor")
{
    const Written minor{aMinorMelody()};
    const auto darker = minor.run(Transform::darker);
    CHECK(rhythmOf(darker) == rhythmOf(minor.source));
    CHECK(meanPitch(darker) < meanPitch(minor.source));
    CHECK(allIn(darker, aMinor));

    // C major, over the same bars: E, A and B become Eb, Ab and Bb.
    const Written major{{note(60, 0.0, 1.0),
                         note(64, 1.0, 1.0),
                         note(67, 2.0, 1.0),
                         note(69, 3.0, 1.0),
                         note(71, 4.0, 2.0),
                         note(72, 6.0, 2.0)}};
    auto constraints = major.constraints;
    constraints.key = {Key{0, Mode::major}, Source::imposed};
    const auto darkened =
        transform(Transform::darker, major.source, major.context, constraints, major.model, 0);
    CHECK(rhythmOf(darkened) == rhythmOf(major.source));
    CHECK(pitchesOf(darkened) == std::vector<int>{60, 63, 67, 68, 70, 72});
    CHECK(allIn(darkened, Key{0, Mode::minor}));
}

TEST_CASE("transform: brighter — same rhythm, higher, in the brighter key")
{
    const Written written{aMinorMelody()};
    const auto brighter = written.run(Transform::brighter);
    CHECK(rhythmOf(brighter) == rhythmOf(written.source));
    CHECK(meanPitch(brighter) > meanPitch(written.source));
    CHECK(allIn(brighter, Key{9, Mode::major}));
}

TEST_CASE("transform: busier — the same pitches in the same order, more attacks")
{
    const Written written{aMinorMelody()};
    const auto after = written.run(Transform::busier);
    CHECK(after.size() > written.source.size());

    auto collapsed = pitchesOf(after);
    collapsed.erase(std::unique(collapsed.begin(), collapsed.end()), collapsed.end());
    auto original = pitchesOf(written.source);
    original.erase(std::unique(original.begin(), original.end()), original.end());
    CHECK(collapsed == original);
}

TEST_CASE("transform: calmer — fewer notes, the ones kept in their order, on the beat")
{
    const Written written{aMinorMelody()};
    const auto after = written.run(Transform::calmer);
    CHECK(after.size() < written.source.size());

    const auto before = pitchesOf(written.source);
    const auto now = pitchesOf(after);
    // A subsequence, in order.
    std::size_t at = 0;
    for (const auto pitch : before)
    {
        if (at < now.size() && now[at] == pitch)
            ++at;
    }
    CHECK(at == now.size());
    for (const auto& ghost : after)
        CHECK(std::abs(ghost.startBeats - std::round(ghost.startBeats)) < 1e-9);
}

TEST_CASE("transform: humanize — the same pitches, every attack within a sixty-fourth, velocities within 8")
{
    const Written written{aMinorMelody()};
    for (int variant = 0; variant < 3; ++variant)
    {
        const auto after = written.run(Transform::humanize, variant);
        REQUIRE(after.size() == written.source.size());
        CHECK(pitchesOf(after) == pitchesOf(written.source));

        auto moved = 0;
        for (std::size_t index = 0; index < after.size(); ++index)
        {
            const auto shift = std::abs(after[index].startBeats - written.source[index].startBeats);
            CHECK(shift <= 1.0 / 64.0 + 1e-9);
            CHECK(std::abs(after[index].velocity - written.source[index].velocity) <= 8);
            moved += shift > 1e-6 ? 1 : 0;
        }
        CHECK(moved * 2 >= static_cast<int>(after.size())); // most attacks move, not one
    }
}

TEST_CASE("transform: the local words that ask for each one")
{
    const auto read = [](const char* text) { return readTransform(text); };
    CHECK(read("garde le rythme, change les notes")->transform == Transform::keepRhythm);
    CHECK(read("mêmes notes, autre rythme")->transform == Transform::keepPitches);
    CHECK(read("une variante légère")->transform == Transform::variation);
    CHECK(read("plus sombre")->transform == Transform::darker);
    CHECK(read("garde le rythme mais plus sombre")->transform == Transform::darker);
    CHECK(read("plus lumineux")->transform == Transform::brighter);
    CHECK(read("plus rythmé")->transform == Transform::busier);
    CHECK(read("plus calme")->transform == Transform::calmer);
    CHECK(read("humanise")->transform == Transform::humanize);
    CHECK_FALSE(read("en ré mineur").has_value());

    const auto words = read("rends-le plus sombre")->words;
    CHECK(words == std::vector<std::string>{"plus", "sombre"});
    CHECK(transformNamed(nameOf(Transform::busier)) == Transform::busier);
}
