#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/commands/AddNote.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/Learning.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/serialization/Json.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;

namespace
{

Note note(int pitch, double start, double length, int velocity = 100)
{
    Note out{};
    out.id = NoteId::generate();
    out.pitch = pitch;
    out.velocity = velocity;
    out.startBeats = start;
    out.lengthBeats = length;
    return out;
}

TrackId addTrack(ProjectState& state, const std::string& name)
{
    Track track{};
    track.id = TrackId::generate();
    track.name = name;
    REQUIRE(state.addTrack(track).ok());
    return track.id;
}

Clip row(TrackId track, std::vector<Note> notes)
{
    Clip clip{};
    clip.id = ClipId::generate();
    clip.trackId = track;
    clip.notes = std::move(notes);
    return clip;
}

// The corpus services/tests/test_corpus.py builds, as two patterns of a
// project: the same phrase in A minor and in F# minor, the chords i VI VII i
// under each, and hats on one pitch under the first.
struct Corpus
{
    Corpus()
    {
        const auto lead = addTrack(state, "Lead");
        const auto keys = addTrack(state, "Keys");
        const auto hats = addTrack(state, "Hats");

        // (step, length, semitones from the tonic), as PHRASE in test_corpus.py.
        const int phrase[9][3] = {{0, 2, 0},
                                  {2, 2, 3},
                                  {4, 4, 7},
                                  {8, 2, 5},
                                  {10, 2, 3},
                                  {12, 4, 2},
                                  {16, 3, 0},
                                  {19, 1, -2},
                                  {20, 4, 0}};
        const int shapes[4][3] = {{0, 3, 7}, {8, 12, 15}, {10, 14, 17}, {0, 3, 7}};

        for (const auto tonic : {9, 6})
        {
            Pattern pattern{};
            pattern.id = PatternId::generate();
            pattern.lengthBeats = 16.0;

            std::vector<Note> line;
            for (const auto& [step, length, offset] : phrase)
                line.push_back(note(60 + tonic + offset, step / 4.0, length / 4.0, step % 4 == 0 ? 100 : 80));
            pattern.clips.push_back(row(lead, line));

            std::vector<Note> chords;
            for (int bar = 0; bar < 4; ++bar)
            {
                for (const auto interval : shapes[bar])
                    chords.push_back(note(48 + tonic + interval, bar * 4.0, 4.0, 90));
            }
            pattern.clips.push_back(row(keys, chords));

            if (tonic == 9)
            {
                std::vector<Note> hits;
                for (int step = 0; step < 32; step += 2)
                    hits.push_back(note(42, step / 4.0, 0.25, 90));
                pattern.clips.push_back(row(hats, hits));
            }
            REQUIRE(state.addPattern(pattern).ok());
        }
    }

    ProjectState state;
};

StyleModel fixture()
{
    std::ifstream file{std::string{DAW_SERVICES_FIXTURES} + "/style-small.json"};
    REQUIRE(file.good());
    std::stringstream text;
    text << file.rdbuf();
    auto value = json::read(text.str());
    REQUIRE(value.ok());
    auto model = StyleModel::fromValue(value.value());
    REQUIRE(model.ok());
    return model.value();
}

void checkSame(const Table& counted, const Table& written, const std::string& what)
{
    CAPTURE(what);
    CHECK(counted.size() == written.size());
    for (const auto& [context, counts] : written)
    {
        CAPTURE(context);
        const auto found = counted.find(context);
        REQUIRE(found != counted.end());
        CHECK(found->second == counts);
    }
}

// --- the two styles of the proof -----------------------------------------------------

// Four patterns of four bars, one lead row each, in A minor.
ProjectState styled(bool eighthsStepwise)
{
    ProjectState state;
    const auto lead = addTrack(state, "Lead");
    const Key key{9, Mode::minor};
    const auto home = *diatonicIndex(69, key);

    // Eighths up and down the scale by step; or sixteenths that leap a fourth
    // or a fifth, one way then the other.
    const int steps[8] = {0, 1, 2, 3, 4, 3, 2, 1};
    const int leaps[8] = {0, 4, 1, 5, 2, -2, 1, -3};

    for (int p = 0; p < 4; ++p)
    {
        Pattern pattern{};
        pattern.id = PatternId::generate();
        pattern.lengthBeats = 16.0;
        std::vector<Note> notes;
        const auto every = eighthsStepwise ? 2 : 1;
        for (int step = 0, i = 0; step < 64; step += every, ++i)
        {
            const auto degree = eighthsStepwise ? steps[i % 8] : leaps[i % 8];
            notes.push_back(
                note(pitchOfIndex(home + degree, key), step / 4.0, every / 4.0, step % 4 == 0 ? 105 : 85));
        }
        pattern.clips.push_back(row(lead, notes));
        REQUIRE(state.addPattern(pattern).ok());
    }
    return state;
}

// An empty lead row over four bars: where the proposals are drawn.
struct Blank
{
    Blank()
    {
        lead = addTrack(state, "Lead");
        Pattern pattern{};
        pattern.id = patternId;
        pattern.lengthBeats = 16.0;
        REQUIRE(state.addPattern(pattern).ok());
    }

    [[nodiscard]] Context context() const
    {
        auto built = Context::of(state, patternId, lead, 0.0, 16.0);
        REQUIRE(built.ok());
        return built.value();
    }

    ProjectState state;
    TrackId lead{};
    PatternId patternId{PatternId::generate()};
};

struct Measures
{
    double meanGap{0.0};      // sixteenths between two onsets
    double eighths{0.0};      // share of gaps of two sixteenths
    double sixteenths{0.0};   // share of gaps of one
    double meanInterval{0.0}; // degrees between two notes, in absolute value
    double steps{0.0};        // share of intervals of one degree or none
    double leaps{0.0};        // share of intervals of three degrees or more
    std::map<int, double> gaps;
    std::map<int, double> intervals;
};

Measures measure(const StyleModel& model)
{
    const Blank blank;
    const auto context = blank.context();
    Constraints wanted{};
    wanted.key = Key{9, Mode::minor};
    wanted.role = Role::melody;
    const auto resolved = resolve(wanted, context);

    Measures out{};
    double gapCount = 0.0;
    double intervalCount = 0.0;
    for (int variant = 0; variant < 16; ++variant)
    {
        const auto notes = generate(context, resolved, model, variant);
        for (std::size_t i = 1; i < notes.size(); ++i)
        {
            const auto gap =
                static_cast<int>(std::lround((notes[i].startBeats - notes[i - 1].startBeats) * 4.0));
            const auto interval = std::abs(*diatonicIndex(notes[i].pitch, resolved.key.value) -
                                           *diatonicIndex(notes[i - 1].pitch, resolved.key.value));
            out.gaps[gap] += 1.0;
            out.intervals[interval] += 1.0;
            out.meanGap += gap;
            out.meanInterval += interval;
            gapCount += 1.0;
            intervalCount += 1.0;
        }
    }
    out.meanGap /= gapCount;
    out.meanInterval /= intervalCount;
    for (auto& [gap, n] : out.gaps)
        n /= gapCount;
    for (auto& [interval, n] : out.intervals)
        n /= intervalCount;
    out.eighths = out.gaps[2];
    out.sixteenths = out.gaps[1];
    out.steps = out.intervals[0] + out.intervals[1];
    for (const auto& [interval, n] : out.intervals)
        out.leaps += interval >= 3 ? n : 0.0;
    return out;
}

// Half the L1 distance between two histograms: 0 the same, 1 nothing in common.
double distance(const std::map<int, double>& a, const std::map<int, double>& b)
{
    std::map<int, double> all = a;
    for (const auto& [x, n] : b)
        static_cast<void>(all[x]);
    double total = 0.0;
    for (const auto& [x, n] : all)
    {
        const auto left = a.count(x) != 0 ? a.at(x) : 0.0;
        const auto right = b.count(x) != 0 ? b.at(x) : 0.0;
        total += std::abs(left - right);
    }
    return total / 2.0;
}

std::string describe(const char* name, const Measures& m)
{
    std::ostringstream out;
    out.precision(2);
    out << std::fixed << name << " : écart moyen " << m.meanGap << " doubles (croches " << m.eighths * 100
        << " %, doubles " << m.sixteenths * 100 << " %), intervalle moyen " << m.meanInterval
        << " degrés (conjoints " << m.steps * 100 << " %, sauts " << m.leaps * 100 << " %)";
    return out.str();
}

} // namespace

TEST_CASE("the counts learned from a project are the counts the corpus pipeline writes")
{
    const Corpus corpus;
    const auto learned = learn(corpus.state);
    const auto written = fixture();

    for (const auto role : {Role::melody, Role::chords, Role::rhythm})
    {
        const auto& counted = learned.role(role).tables;
        const auto& expected = written.role(role);
        const std::string name{describe(role)};
        checkSame(counted.rhythm, expected.rhythm, name + ".rhythm");
        checkSame(counted.duration, expected.duration, name + ".duration");
        checkSame(counted.interval, expected.interval, name + ".interval");
        checkSame(counted.degree, expected.degree, name + ".degree");
        checkSame(counted.progression, expected.progression, name + ".progression");

        const auto velocity = learned.role(role).velocityStyle();
        CHECK(velocity.size() == expected.velocity.size());
        for (const auto& [position, pair] : expected.velocity)
        {
            REQUIRE(velocity.count(position) == 1);
            CHECK(velocity.at(position).first == doctest::Approx(pair.first).epsilon(1e-3));
            CHECK(velocity.at(position).second == doctest::Approx(pair.second).epsilon(1e-3));
        }
    }
    CHECK(learned.role(Role::bass).notes == 0.0);
    CHECK(learned.role(Role::melody).notes == 18.0);
}

TEST_CASE("what was learned goes through its JSON form unchanged and adds up with a weight")
{
    const Corpus corpus;
    const auto learned = learn(corpus.state);
    auto back = Learned::fromValue(json::read(json::write(learned.toValue())).value());
    REQUIRE(back.ok());
    CHECK(back.value() == learned);

    Learned twice{};
    twice.add(learned, 2.0);
    CHECK(twice.role(Role::melody).notes == 36.0);
    CHECK(twice.role(Role::melody).tables.interval.at("").at(2) == doctest::Approx(8.0));
}

TEST_CASE("notes the machine wrote are not learned until the person touches them")
{
    ProjectState state;
    const auto lead = addTrack(state, "Lead");
    Pattern pattern{};
    pattern.id = PatternId::generate();
    pattern.lengthBeats = 16.0;

    std::vector<Note> mine;
    for (int i = 0; i < 4; ++i)
        mine.push_back(note(69 + (i % 2) * 2, i * 1.0, 1.0));
    // The generator wrote two notes; the person has moved the second since.
    const auto generated = note(72, 5.0, 0.5);
    const auto moved = note(74, 6.0, 0.5);
    auto clip = row(lead, mine);
    clip.notes.push_back(generated);
    auto touched = moved;
    touched.startBeats = 7.0;
    clip.notes.push_back(touched);
    pattern.clips.push_back(clip);
    REQUIRE(state.addPattern(pattern).ok());

    MachineNotes machine;
    for (const auto& written : {generated, moved})
    {
        Receipt receipt{};
        receipt.type = "note.add";
        receipt.origin.actor = Actor::generator;
        receipt.payload = AddNote{clip.id, written}.payload();
        machine.observe(receipt);
    }
    Receipt byUser{};
    byUser.type = "note.add";
    byUser.payload = AddNote{clip.id, mine.front()}.payload();
    machine.observe(byUser);
    CHECK(machine.notes().size() == 2);

    CHECK(learn(state).role(Role::melody).notes == 6.0);
    CHECK(learn(state, machine.notes()).role(Role::melody).notes == 5.0);

    // The journal of a project reopened says the same.
    MachineNotes reopened;
    CommandEnvelope envelope{};
    envelope.id = CommandId::generate();
    envelope.type = "note.add";
    envelope.origin.actor = Actor::copilot;
    envelope.payload = AddNote{clip.id, generated}.payload();
    reopened.read({envelope.toValue()});
    CHECK(learn(state, reopened.notes()).role(Role::melody).notes == 5.0);
}

TEST_CASE("with nothing learned the base plays, and the share grows with every note")
{
    const auto base = StyleModel::fallback();
    const auto same = blend(base, Learned{});
    CHECK(same.origin() == "repli");
    CHECK(json::write(same.toValue()) == json::write(base.toValue()));

    const auto eighths = learn(styled(true));
    CHECK(learnedShare(eighths, Role::melody) == doctest::Approx(128.0 / 328.0));
    CHECK(learnedShare(eighths, Role::bass) == 0.0);

    // No jump: the proposals move towards the learned rhythm a little at a
    // time as the weight of what was learned grows.
    double previous = measure(base).eighths;
    MESSAGE("croches, repli : ", previous);
    for (const auto weight : {0.25, 0.5, 1.0, 2.0, 4.0})
    {
        Learned scaled{};
        scaled.add(eighths, weight);
        const auto now = measure(blend(base, scaled)).eighths;
        MESSAGE(
            "croches, poids ", weight, " (part apprise ", learnedShare(scaled, Role::melody), ") : ", now);
        CHECK(now >= previous - 0.05);
        CHECK(now - previous <= 0.35);
        previous = now;
    }
}

TEST_CASE("two projects in two styles give two measurably different generators")
{
    const auto base = StyleModel::fallback();

    // What the application does for the project open: its counts, three times.
    Learned fromEighths{};
    fromEighths.add(learn(styled(true)), currentProjectWeight);
    Learned fromLeaps{};
    fromLeaps.add(learn(styled(false)), currentProjectWeight);

    const auto a = measure(blend(base, fromEighths));
    const auto b = measure(blend(base, fromLeaps));
    const auto fallback = measure(base);
    MESSAGE(describe("repli", fallback));
    MESSAGE(describe("projet A, croches conjointes", a));
    MESSAGE(describe("projet B, doubles en sauts", b));
    MESSAGE("distance des écarts A/B ",
            distance(a.gaps, b.gaps),
            ", des intervalles A/B ",
            distance(a.intervals, b.intervals));

    // Thresholds written before the first run.
    CHECK(a.eighths >= 0.6);
    CHECK(b.sixteenths >= 0.6);
    CHECK(a.steps >= 0.6);
    CHECK(b.leaps >= 0.5);
    CHECK(a.meanGap > b.meanGap + 0.5);
    CHECK(b.meanInterval > a.meanInterval + 1.0);
    CHECK(distance(a.gaps, b.gaps) >= 0.5);
    CHECK(distance(a.intervals, b.intervals) >= 0.5);

    // Every proposal is still legal: learning ranks, the rules decide.
    const Blank blank;
    Constraints wanted{};
    wanted.key = Key{9, Mode::minor};
    wanted.role = Role::melody;
    const auto resolved = resolve(wanted, blank.context());
    for (const auto* model : {&fromEighths, &fromLeaps})
    {
        const auto blended = blend(base, *model);
        for (int variant = 0; variant < 8; ++variant)
        {
            for (const auto& ghost : generate(blank.context(), resolved, blended, variant))
                CHECK(inScale(ghost.pitch, resolved.key.value));
        }
    }
}
