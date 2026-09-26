#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/serialization/Json.h"

#include <chrono>
#include <cmath>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;

namespace
{

Note note(int pitch, double start, double length)
{
    Note out{};
    out.id = NoteId::generate();
    out.pitch = pitch;
    out.velocity = 100;
    out.startBeats = start;
    out.lengthBeats = length;
    return out;
}

// A project of four bars with a lead row to write, and a chord row playing
// A minor, F major, G major, A minor, one bar each.
struct Song
{
    Song()
    {
        Track leadTrack{};
        leadTrack.id = lead;
        leadTrack.name = "Lead";
        REQUIRE(state.addTrack(leadTrack).ok());

        Track keysTrack{};
        keysTrack.id = keys;
        keysTrack.name = "Keys";
        REQUIRE(state.addTrack(keysTrack).ok());

        Pattern pattern{};
        pattern.id = patternId;
        pattern.lengthBeats = 16.0;

        Clip leadRow{};
        leadRow.id = ClipId::generate();
        leadRow.trackId = lead;
        pattern.clips.push_back(leadRow);

        Clip keysRow{};
        keysRow.id = ClipId::generate();
        keysRow.trackId = keys;
        const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 59, 62}, {57, 60, 64}};
        for (int bar = 0; bar < 4; ++bar)
        {
            for (const auto pitch : chords[bar])
                keysRow.notes.push_back(note(pitch, bar * 4.0, 4.0));
        }
        pattern.clips.push_back(keysRow);

        REQUIRE(state.addPattern(pattern).ok());
    }

    [[nodiscard]] Context context(double from = 0.0, double to = 16.0) const
    {
        auto built = Context::of(state, patternId, lead, from, to);
        REQUIRE(built.ok());
        return built.value();
    }

    ProjectState state;
    TrackId lead{TrackId::generate()};
    TrackId keys{TrackId::generate()};
    PatternId patternId{PatternId::generate()};
};

bool onGrid(double beats, int steps)
{
    const auto units = beats / (stepBeats * steps);
    return std::abs(units - std::round(units)) < 1e-9;
}

} // namespace

// --- the interpreter -----------------------------------------------------------------

TEST_CASE("the local interpreter reads the short words and says what it ignored")
{
    const auto read = LocalInterpreter::parse("F#m doubles dense grave basse sombre");
    REQUIRE(read.constraints.key.has_value());
    CHECK(read.constraints.key->tonic == 6);
    CHECK(read.constraints.key->mode == Mode::minor);
    CHECK(read.constraints.resolution == Resolution::sixteenth);
    CHECK(read.constraints.density == Density::dense);
    CHECK(read.constraints.reg == Register::low);
    CHECK(read.constraints.role == Role::bass);
    REQUIRE(read.ignored.size() == 1);
    CHECK(read.ignored.front() == "sombre");
    CHECK(read.conflicts.empty());
}

TEST_CASE("the local interpreter keeps the last of two words for one field and says so")
{
    const auto read = LocalInterpreter::parse("croches Am doubles");
    CHECK(read.constraints.resolution == Resolution::sixteenth);
    REQUIRE(read.conflicts.size() == 1);
    CHECK(read.conflicts.front() == "croches puis doubles : doubles retenu");
}

TEST_CASE("an empty text is no constraint at all")
{
    const auto read = LocalInterpreter::parse("   ");
    CHECK(read.constraints == Constraints{});
    CHECK(read.ignored.empty());
}

TEST_CASE("the interpreter answers through its callback")
{
    LocalInterpreter interpreter;
    std::optional<Interpretation> answer;
    interpreter.interpret("Bbm accords", [&answer](Interpretation read) { answer = std::move(read); });
    REQUIRE(answer.has_value());
    CHECK(answer->constraints.key == Key{10, Mode::minor});
    CHECK(answer->constraints.role == Role::chords);
}

TEST_CASE("the constraints travel as the JSON the S15 copilot will send")
{
    const auto text =
        std::string{R"({"key":{"tonic":"A","mode":"minor"},"resolution":"1/16",)"} +
        R"("density":"dense","register":"low","role":"bass","ignored":["sombre"],"conflicts":[]})";
    auto value = json::read(text);
    REQUIRE(value.ok());

    auto read = Interpretation::fromValue(value.value());
    REQUIRE(read.ok());
    CHECK(read.value().constraints.key == Key{9, Mode::minor});
    CHECK(read.value().constraints.role == Role::bass);
    CHECK(read.value().ignored == std::vector<std::string>{"sombre"});

    // And back, to the same thing.
    auto again = Interpretation::fromValue(read.value().toValue());
    REQUIRE(again.ok());
    CHECK(again.value() == read.value());

    CHECK_FALSE(Constraints::fromValue(json::read(R"({"role":"guitare"})").value()).ok());
}

// --- the rules -----------------------------------------------------------------------

TEST_CASE("a diatonic index and its pitch are the two sides of one thing")
{
    const Key aMinor{9, Mode::minor};
    for (int pitch = 0; pitch <= 127; ++pitch)
    {
        const auto index = diatonicIndex(pitch, aMinor);
        CHECK(index.has_value() == inScale(pitch, aMinor));
        if (index.has_value())
            CHECK(pitchOfIndex(*index, aMinor) == pitch);
    }
    CHECK(inScale(69, aMinor));
    CHECK_FALSE(inScale(68, aMinor)); // G#: harmonic minor is not in the scale
}

TEST_CASE("the key and the chords are read from what sounds")
{
    std::vector<WeightedPitch> aMinor;
    for (const auto pitch : {57, 60, 64, 57, 59, 62, 64, 65, 67})
        aMinor.push_back({pitch, 1.0});
    CHECK(detectKey(aMinor) == Key{9, Mode::minor});
    CHECK_FALSE(detectKey({}).has_value());

    const Key key{9, Mode::minor};
    CHECK(detectChord({{53, 1.0}, {57, 1.0}, {60, 1.0}}, key) == Chord{5});
    CHECK(detectChord({{57, 1.0}, {60, 1.0}, {64, 1.0}}, key) == Chord{0});
}

// --- resolution ----------------------------------------------------------------------

TEST_CASE("an empty zone generates with what the context says")
{
    const Song song;
    const auto resolved = resolve({}, song.context());
    CHECK(resolved.key.value == Key{9, Mode::minor});
    CHECK(resolved.key.source == Source::deduced);
    CHECK(resolved.role.value == Role::melody);
    CHECK(resolved.role.source == Source::deduced); // "Lead"
    CHECK(resolved.resolution.value == Resolution::sixteenth);
    CHECK(resolved.resolution.source == Source::defaulted);

    Constraints imposed{};
    imposed.key = Key{2, Mode::minor};
    const auto forced = resolve(imposed, song.context());
    CHECK(forced.key.value == Key{2, Mode::minor});
    CHECK(forced.key.source == Source::imposed);
}

TEST_CASE("the chords of each bar are read from the other rows")
{
    const Song song;
    const auto chords = chordsOf(song.context(), Key{9, Mode::minor});
    REQUIRE(chords.size() == 4);
    CHECK(chords[0] == Chord{0}); // Am
    CHECK(chords[1] == Chord{5}); // F
    CHECK(chords[2] == Chord{6}); // G
    CHECK(chords[3] == Chord{0}); // Am
}

// --- generation ----------------------------------------------------------------------

TEST_CASE("every generated note is in the key, in the register, in the range and on the grid")
{
    const Song song;
    const auto model = StyleModel::fallback();

    for (const auto role : {Role::melody, Role::bass, Role::chords})
    {
        for (const auto resolution : {Resolution::quarter, Resolution::eighth, Resolution::sixteenth})
        {
            for (const auto density : {Density::sparse, Density::medium, Density::dense})
            {
                Constraints wanted{};
                wanted.key = Key{6, Mode::minor};
                wanted.role = role;
                wanted.resolution = resolution;
                wanted.density = density;
                const auto context = song.context(4.0, 12.0);
                const auto resolved = resolve(wanted, context);
                const auto [low, high] = registerRange(role, resolved.reg.value);

                for (int variant = 0; variant < 8; ++variant)
                {
                    const auto notes = generate(context, resolved, model, variant);
                    CHECK_FALSE(notes.empty());
                    for (const auto& ghost : notes)
                    {
                        CHECK(inScale(ghost.pitch, Key{6, Mode::minor}));
                        CHECK(ghost.pitch >= low);
                        CHECK(ghost.pitch <= high);
                        CHECK(ghost.startBeats >= 4.0);
                        CHECK(ghost.startBeats + ghost.lengthBeats <= 12.0 + 1e-9);
                        CHECK(onGrid(ghost.startBeats, stepsOf(resolution)));
                        CHECK(ghost.lengthBeats > 0.0);
                        CHECK(ghost.velocity >= Note::lowestVelocity);
                        CHECK(ghost.velocity <= Note::highestVelocity);
                    }
                }
            }
        }
    }
}

TEST_CASE("a melody never overlaps itself and chords are triads of the key")
{
    const Song song;
    const auto model = StyleModel::fallback();

    Constraints melody{};
    melody.role = Role::melody;
    const auto line = generate(song.context(), resolve(melody, song.context()), model, 3);
    for (std::size_t i = 1; i < line.size(); ++i)
        CHECK(line[i - 1].startBeats + line[i - 1].lengthBeats <= line[i].startBeats + 1e-9);

    Constraints chords{};
    chords.role = Role::chords;
    chords.key = Key{9, Mode::minor};
    const auto stacked = generate(song.context(), resolve(chords, song.context()), model, 0);
    REQUIRE(stacked.size() % 3 == 0);
    for (std::size_t i = 0; i < stacked.size(); i += 3)
    {
        CHECK(stacked[i].startBeats == stacked[i + 1].startBeats);
        CHECK(stacked[i].startBeats == stacked[i + 2].startBeats);
        // Each chord is the one the keys play in that bar.
        const auto bar = static_cast<std::size_t>(stacked[i].startBeats / 4.0);
        const Chord heard[4] = {Chord{0}, Chord{5}, Chord{6}, Chord{0}};
        for (std::size_t voice = 0; voice < 3; ++voice)
            CHECK(isChordTone(stacked[i + voice].pitch, heard[bar], Key{9, Mode::minor}));
    }
}

TEST_CASE("a rhythm channel keeps its own pitch")
{
    Song song;
    Constraints wanted{};
    wanted.role = Role::rhythm;
    auto context = song.context();
    context.channelPitch = 42;
    const auto notes = generate(context, resolve(wanted, context), StyleModel::fallback(), 0);
    REQUIRE_FALSE(notes.empty());
    for (const auto& ghost : notes)
        CHECK(ghost.pitch == 42);
}

TEST_CASE("the same inputs give the same notes, and variants differ")
{
    const Song song;
    const auto model = StyleModel::fallback();
    const auto context = song.context();
    const auto resolved = resolve({}, context);

    CHECK(generate(context, resolved, model, 0) == generate(context, resolved, model, 0));

    Variants variants{context, resolved, model};
    std::set<std::vector<int>> seen;
    for (int rank = 0; rank < 6; ++rank)
    {
        std::vector<int> pitches;
        for (const auto& ghost : variants.at(rank))
            pitches.push_back(ghost.pitch * 1000 + static_cast<int>(ghost.startBeats * 4));
        seen.insert(pitches);
    }
    CHECK(seen.size() == 6);
    CHECK(variants.drawn() >= 6);
}

TEST_CASE("the context hash changes with the notes and only with them")
{
    Song song;
    const auto before = song.context().hash();
    CHECK(song.context().hash() == before);

    const auto* pattern = song.state.findPattern(song.patternId);
    REQUIRE(pattern != nullptr);
    REQUIRE(song.state.addNote(pattern->clips.front().id, note(72, 0.0, 1.0)).ok());
    CHECK(song.context().hash() != before);
}

TEST_CASE("an empty range is refused")
{
    const Song song;
    CHECK_FALSE(Context::of(song.state, song.patternId, song.lead, 3.0, 3.0).ok());
    CHECK_FALSE(Context::of(song.state, PatternId::generate(), song.lead, 0.0, 4.0).ok());
}

TEST_CASE("four bars of sixteenths are generated well under a frame")
{
    const Song song;
    const auto model = StyleModel::fallback();
    const auto context = song.context();
    Constraints dense{};
    dense.density = Density::dense;
    dense.resolution = Resolution::sixteenth;
    const auto resolved = resolve(dense, context);

    const auto started = std::chrono::steady_clock::now();
    for (int variant = 0; variant < 10; ++variant)
        static_cast<void>(generate(context, resolved, model, variant));
    const auto each =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started) / 10.0;

    MESSAGE("one generation: " << each.count() << " ms");
    CHECK(each.count() < 16.0);
}

// --- the style model -----------------------------------------------------------------

TEST_CASE("the style model goes through its JSON form unchanged")
{
    const auto model = StyleModel::fallback();
    auto text = json::write(model.toValue());
    auto value = json::read(text);
    REQUIRE(value.ok());

    auto back = StyleModel::fromValue(value.value());
    REQUIRE(back.ok());
    CHECK(back.value().toValue() == model.toValue());
    CHECK(back.value().origin() == "repli");

    CHECK_FALSE(StyleModel::fromValue(json::read(R"({"format":"other","version":1})").value()).ok());
}

TEST_CASE("the backoff trusts a context in proportion to what it saw")
{
    Table table;
    table["|"] = {{2, 10.0}, {4, 10.0}};
    table["p0|"] = {{4, 1000.0}};

    const auto seen = smoothed(table, rhythmContexts(0, {}), 4, 4);
    const auto unseen = smoothed(table, rhythmContexts(0, {}), 3, 4);
    const auto elsewhere = smoothed(table, rhythmContexts(5, {}), 4, 4);
    CHECK(seen > 0.99);
    CHECK(unseen > 0.0);
    CHECK(unseen < 0.01);
    CHECK(elsewhere == doctest::Approx((10.0 + 2.0 * 0.25) / 22.0));

    CHECK(rhythmContexts(4, {2, 2, 1, 3}) ==
          std::vector<std::string>{"p4|2,1,3", "p4|1,3", "p4|3", "p4|", "|"});
    CHECK(intervalContexts({1, -2}) == std::vector<std::string>{"1,-2", "-2", ""});
}
