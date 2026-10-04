#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/Harmony.h"
#include "daw/domain/generation/StyleModel.h"

#include <algorithm>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using namespace daw::domain::generation;

namespace
{

// An empty lead row in a pattern of four bars, and nothing else: no note says
// a key, the direction is the only thing that can.
struct Project
{
    Project()
    {
        Track leadTrack{};
        leadTrack.id = lead;
        leadTrack.name = "Lead";
        REQUIRE(state.addTrack(leadTrack).ok());
        Pattern pattern{};
        pattern.id = patternId;
        pattern.lengthBeats = 16.0;
        REQUIRE(state.addPattern(pattern).ok());
    }

    void direct(Key key, double vocalsPlay, double amount = 0.5)
    {
        direction::Direction wanted;
        direction::Reference reference;
        reference.reading.name = "ref.wav";
        reference.reading.digest = std::string(64, 'e');
        reference.reading.key = key;
        reference.reading.stems["vocals"] = direction::StemReading{-14.0, -3.0, vocalsPlay};
        wanted.references.push_back(reference);
        wanted.amount = amount;
        state.setDirection(wanted);
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

constexpr Key eMajor{4, Mode::major};

bool inKey(int pitch, Key key)
{
    return diatonicIndex(pitch, key).has_value();
}

// Every note of eight variants.
std::vector<GhostNote> eightVariants(const Context& context, const ResolvedConstraints& resolved)
{
    const auto model = StyleModel::fallback();
    std::vector<GhostNote> all;
    for (int variant = 0; variant < 8; ++variant)
    {
        const auto notes = generate(context, resolved, model, variant);
        all.insert(all.end(), notes.begin(), notes.end());
    }
    return all;
}

} // namespace

TEST_CASE("Generation with a direction: its key, said to come from the reference, and every note in it")
{
    Project project;
    project.direct(eMajor, 0.6);
    const auto context = project.context();
    const auto resolved = resolve(Constraints{}, context);
    CHECK(resolved.key.value == eMajor);
    CHECK(resolved.key.source == Source::directed);

    const auto notes = eightVariants(context, resolved);
    REQUIRE(!notes.empty());
    for (const auto& note : notes)
        CHECK(inKey(note.pitch, eMajor));

    // The effect, not the label: without the direction, the default key has
    // notes E major does not.
    Project plain;
    const auto plainResolved = resolve(Constraints{}, plain.context());
    CHECK(plainResolved.key.source == Source::defaulted);
    const auto plainNotes = eightVariants(plain.context(), plainResolved);
    const auto outside = std::count_if(plainNotes.begin(),
                                       plainNotes.end(),
                                       [](const GhostNote& note) { return !inKey(note.pitch, eMajor); });
    CHECK(outside > 0);
}

TEST_CASE("Generation with a direction: what the user wrote and the notes around still come first")
{
    Project project;
    project.direct(eMajor, 0.6);
    Constraints asked{};
    asked.key = Key{9, Mode::minor};
    CHECK(resolve(asked, project.context()).key.source == Source::imposed);
}

TEST_CASE("Generation with a direction: a voice heard a fifth of the reference asks for a sparse melody")
{
    Project sparse;
    sparse.direct(eMajor, 0.2);
    const auto sparseResolved = resolve(Constraints{}, sparse.context());
    CHECK(sparseResolved.density.value == Density::sparse);
    CHECK(sparseResolved.density.source == Source::directed);

    Project dense;
    dense.direct(eMajor, 0.95);
    const auto denseResolved = resolve(Constraints{}, dense.context());
    CHECK(denseResolved.density.value == Density::dense);

    // At the effect: fewer notes.
    const auto few = eightVariants(sparse.context(), sparseResolved).size();
    const auto many = eightVariants(dense.context(), denseResolved).size();
    MESSAGE("notes sur huit variantes : " << few << " clair, " << many << " dense");
    CHECK(few < many);
}

TEST_CASE("Generation with a direction at 0: the direction is not read")
{
    Project project;
    project.direct(eMajor, 0.2, 0.0);
    const auto resolved = resolve(Constraints{}, project.context());
    CHECK(resolved.key.source == Source::defaulted);
    CHECK(resolved.density.source == Source::defaulted);
}

TEST_CASE("Generation with a direction: a new reference changes the context's hash")
{
    Project project;
    const auto before = project.context().hash();
    project.direct(eMajor, 0.6);
    CHECK(project.context().hash() != before);
}
