#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Form.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"

#include <cmath>
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
    out.startBeats = start;
    out.lengthBeats = length;
    return out;
}

// A pattern of four bars with an empty lead row, with or without a keys row
// playing Am | F | G | Am.
struct Project
{
    explicit Project(bool withChords)
    {
        Track leadTrack{};
        leadTrack.id = lead;
        leadTrack.name = "Lead";
        REQUIRE(state.addTrack(leadTrack).ok());

        Pattern pattern{};
        pattern.id = patternId;
        pattern.lengthBeats = 16.0;

        if (withChords)
        {
            Track keysTrack{};
            keysTrack.id = TrackId::generate();
            keysTrack.name = "Keys";
            REQUIRE(state.addTrack(keysTrack).ok());

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
        }

        REQUIRE(state.addPattern(pattern).ok());
    }

    [[nodiscard]] Context context() const
    {
        auto built = Context::of(state, patternId, lead, 0.0, 16.0);
        REQUIRE(built.ok());
        return built.value();
    }

    ProjectState state;
    TrackId lead{TrackId::generate()};
    PatternId patternId{PatternId::generate()};
};

// The rhythmic motif of a bar: the sixteenths its notes start on, each once.
std::vector<int> motif(const std::vector<GhostNote>& notes, int bar)
{
    std::set<int> steps;
    for (const auto& ghost : notes)
    {
        if (ghost.startBeats >= bar * 4.0 - 1e-9 && ghost.startBeats < (bar + 1) * 4.0 - 1e-9)
            steps.insert(static_cast<int>(std::lround((ghost.startBeats - bar * 4.0) * 4.0)));
    }
    return {steps.begin(), steps.end()};
}

std::vector<GhostNote> barOf(const std::vector<GhostNote>& notes, int bar)
{
    std::vector<GhostNote> out;
    for (auto ghost : notes)
    {
        if (ghost.startBeats >= bar * 4.0 - 1e-9 && ghost.startBeats < (bar + 1) * 4.0 - 1e-9)
        {
            ghost.startBeats -= bar * 4.0;
            out.push_back(ghost);
        }
    }
    return out;
}

ResolvedConstraints wanted(const Context& context, Role role, std::optional<Form> form)
{
    Constraints constraints{};
    constraints.role = role;
    constraints.key = Key{9, Mode::minor};
    constraints.form = form;
    return resolve(constraints, context);
}

} // namespace

TEST_CASE("the form is chosen from the role and the length of the range")
{
    CHECK(defaultForm(Role::melody, 1) == Form::free);
    CHECK(defaultForm(Role::melody, 2) == Form::aaPrime);
    CHECK(defaultForm(Role::melody, 3) == Form::aab);
    CHECK(defaultForm(Role::melody, 4) == Form::aaba);
    CHECK(defaultForm(Role::bass, 4) == Form::aaab);
    CHECK(defaultForm(Role::rhythm, 4) == Form::loop);
    CHECK(defaultForm(Role::chords, 2) == Form::loop);

    CHECK(unitBars(Role::melody, 4) == 1);
    CHECK(unitBars(Role::melody, 8) == 2);
    CHECK(unitBars(Role::bass, 8) == 1);

    CHECK(schema(Form::aaba, 4) == std::vector<Letter>{Letter::a, Letter::a, Letter::b, Letter::aReturn});
    CHECK(schema(Form::aaab, 4) == std::vector<Letter>{Letter::a, Letter::a, Letter::a, Letter::b});
    CHECK(schema(Form::aaPrime, 2) == std::vector<Letter>{Letter::a, Letter::aVaried});
    CHECK(schema(Form::loop, 3) == std::vector<Letter>{Letter::a, Letter::a, Letter::a});
    CHECK(schema(Form::free, 4).empty());
    CHECK(schema(Form::aaba, 1).empty());

    // Four bars of an empty lead row: AABA, deduced, and the zone says it.
    const Project project{false};
    const auto resolved = resolve({}, project.context());
    CHECK(resolved.form.value == Form::aaba);
    CHECK(resolved.form.source == Source::deduced);
    CHECK(describe(resolved).find("AABA (déduit)") != std::string::npos);
}

TEST_CASE("the interpreter reads the form words and the JSON carries them")
{
    CHECK(LocalInterpreter::parse("AABA").constraints.form == Form::aaba);
    CHECK(LocalInterpreter::parse("aaab").constraints.form == Form::aaab);
    CHECK(LocalInterpreter::parse("AA'").constraints.form == Form::aaPrime);
    CHECK(LocalInterpreter::parse("AA\xE2\x80\xB2").constraints.form == Form::aaPrime);
    CHECK(LocalInterpreter::parse("boucle").constraints.form == Form::loop);
    CHECK(LocalInterpreter::parse("varié").constraints.form == Form::varied);
    CHECK(LocalInterpreter::parse("libre").constraints.form == Form::free);

    const auto read = LocalInterpreter::parse("Am AABA doubles mélodie");
    CHECK(read.ignored.empty());
    CHECK(read.constraints.key == Key{9, Mode::minor});

    const auto conflict = LocalInterpreter::parse("boucle AABA");
    CHECK(conflict.constraints.form == Form::aaba);
    REQUIRE(conflict.conflicts.size() == 1);

    auto back = Interpretation::fromValue(read.toValue());
    REQUIRE(back.ok());
    CHECK(back.value() == read);

    // A JSON written before the form existed still reads, with no form.
    auto old = Interpretation::fromValue(LocalInterpreter::parse("Am basse").toValue());
    REQUIRE(old.ok());
    CHECK_FALSE(old.value().constraints.form.has_value());
}

TEST_CASE("in AABA the bars 1 2 and 4 share their rhythm and the bar 3 does not")
{
    const auto model = StyleModel::fallback();
    for (const auto withChords : {false, true})
    {
        const Project project{withChords};
        const auto context = project.context();
        for (const auto role : {Role::melody, Role::bass})
        {
            const auto resolved = wanted(context, role, Form::aaba);
            for (int variant = 0; variant < 16; ++variant)
            {
                CAPTURE(withChords);
                CAPTURE(static_cast<int>(role));
                CAPTURE(variant);
                const auto notes = generate(context, resolved, model, variant);
                const auto first = motif(notes, 0);
                REQUIRE_FALSE(first.empty());
                CHECK(motif(notes, 1) == first);
                CHECK(motif(notes, 3) == first);
                CHECK(motif(notes, 2) != first);
            }
        }
    }
}

TEST_CASE("without chords a repeated A is the same notes, and the last A closes on the tonic")
{
    const Project project{false};
    const auto context = project.context();
    const auto resolved = wanted(context, Role::melody, Form::aaba);
    const auto model = StyleModel::fallback();

    for (int variant = 0; variant < 16; ++variant)
    {
        const auto notes = generate(context, resolved, model, variant);
        const auto first = barOf(notes, 0);
        CHECK(barOf(notes, 1) == first);

        auto last = barOf(notes, 3);
        REQUIRE(last.size() == first.size());
        CHECK(diatonicIndex(last.back().pitch, Key{9, Mode::minor}).value() % 7 == 0);
        last.back().pitch = first.back().pitch;
        CHECK(last == first);
    }
}

TEST_CASE("AA prime changes one thing and redraws nothing")
{
    const Project project{false};
    const auto context = project.context();
    const auto model = StyleModel::fallback();

    for (const auto role : {Role::melody, Role::bass, Role::rhythm})
    {
        const auto resolved = wanted(context, role, Form::aaPrime);
        for (int variant = 0; variant < 16; ++variant)
        {
            const auto notes = generate(context, resolved, model, variant);
            const auto a = barOf(notes, 0);
            const auto varied = barOf(notes, 1);
            REQUIRE(a.size() == varied.size());

            // A moved note also shortens the one before it: two at most.
            int differing = 0;
            for (std::size_t i = 0; i < a.size(); ++i)
                differing += a[i] == varied[i] ? 0 : 1;
            CHECK(differing >= 1);
            CHECK(differing <= 2);
        }
    }
}

TEST_CASE("a loop repeats a rhythm channel strictly and the comping of chords bar after bar")
{
    const Project project{true};
    auto context = project.context();
    context.sampleChannel = true;
    context.channelPitch = 42;
    const auto model = StyleModel::fallback();

    const auto hats = wanted(context, Role::rhythm, std::nullopt);
    CHECK(hats.form.value == Form::loop);
    for (int variant = 0; variant < 8; ++variant)
    {
        const auto notes = generate(context, hats, model, variant);
        for (int bar = 1; bar < 4; ++bar)
            CHECK(barOf(notes, bar) == barOf(notes, 0));
    }

    const auto chords = wanted(project.context(), Role::chords, std::nullopt);
    CHECK(chords.form.value == Form::loop);
    for (int variant = 0; variant < 8; ++variant)
    {
        const auto notes = generate(project.context(), chords, model, variant);
        for (int bar = 1; bar < 4; ++bar)
            CHECK(motif(notes, bar) == motif(notes, 0));
    }
}

TEST_CASE("a bass repeated over another chord follows its root")
{
    const Project project{true};
    const auto context = project.context();
    const auto resolved = wanted(context, Role::bass, Form::loop);
    const auto model = StyleModel::fallback();
    const auto key = Key{9, Mode::minor};

    for (int variant = 0; variant < 8; ++variant)
    {
        const auto notes = generate(context, resolved, model, variant);
        const auto am = barOf(notes, 0);
        const auto f = barOf(notes, 1);
        REQUIRE(am.size() == f.size());
        // Am to F: two degrees down, the short way. Same contour, same rhythm.
        for (std::size_t i = 0; i < am.size(); ++i)
        {
            CHECK(f[i].startBeats == am[i].startBeats);
            const auto shift =
                (((*diatonicIndex(f[i].pitch, key) - *diatonicIndex(am[i].pitch, key)) % 7) + 7) % 7;
            CHECK(shift == 5);
        }
    }
}

TEST_CASE("without a form the bars repeat by chance only")
{
    // The effect, measured: the same four bars, sixteen variants, free and in
    // AABA. The free form is what S14 played.
    const Project project{false};
    const auto context = project.context();
    const auto model = StyleModel::fallback();

    int freeRepeats = 0;
    int shapedRepeats = 0;
    for (int variant = 0; variant < 16; ++variant)
    {
        const auto free = generate(context, wanted(context, Role::melody, Form::free), model, variant);
        const auto shaped = generate(context, wanted(context, Role::melody, Form::aaba), model, variant);
        freeRepeats += motif(free, 1) == motif(free, 0) ? 1 : 0;
        shapedRepeats += motif(shaped, 1) == motif(shaped, 0) ? 1 : 0;
    }
    MESSAGE("bar 2 repeats bar 1: free ", freeRepeats, "/16, AABA ", shapedRepeats, "/16");
    CHECK(freeRepeats <= 4);
    CHECK(shapedRepeats == 16);
}

TEST_CASE("every form stays in the key, the register, the range and the grid")
{
    const Project project{true};
    const auto model = StyleModel::fallback();
    const auto key = Key{9, Mode::minor};

    for (const auto role : {Role::melody, Role::bass, Role::chords})
    {
        for (const auto form :
             {Form::free, Form::loop, Form::varied, Form::aaPrime, Form::aab, Form::aaba, Form::aaab})
        {
            for (const auto resolution : {Resolution::eighth, Resolution::sixteenth})
            {
                Constraints constraints{};
                constraints.role = role;
                constraints.key = key;
                constraints.form = form;
                constraints.resolution = resolution;
                // A range that does not fall on a bar: three bars and a half.
                auto built = Context::of(project.state, project.patternId, project.lead, 0.0, 14.0);
                REQUIRE(built.ok());
                const auto resolved = resolve(constraints, built.value());
                const auto [low, high] = registerRange(role, resolved.reg.value);

                for (int variant = 0; variant < 6; ++variant)
                {
                    const auto notes = generate(built.value(), resolved, model, variant);
                    CHECK_FALSE(notes.empty());
                    for (std::size_t i = 0; i < notes.size(); ++i)
                    {
                        const auto& ghost = notes[i];
                        CHECK(inScale(ghost.pitch, key));
                        CHECK(ghost.pitch >= low);
                        CHECK(ghost.pitch <= high);
                        CHECK(ghost.startBeats >= 0.0);
                        CHECK(ghost.startBeats + ghost.lengthBeats <= 14.0 + 1e-9);
                        CHECK(ghost.lengthBeats > 0.0);
                        const auto units = ghost.startBeats / (stepBeats * stepsOf(resolution));
                        CHECK(std::abs(units - std::round(units)) < 1e-9);
                        if (role != Role::chords && i > 0)
                            CHECK(notes[i - 1].startBeats + notes[i - 1].lengthBeats <=
                                  ghost.startBeats + 1e-9);
                    }
                }
            }
        }
    }
}
