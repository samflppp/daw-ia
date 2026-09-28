#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Phrase.h"

#include <string>

#include <doctest/doctest.h>

using namespace daw::domain::generation;

namespace
{

ResolvedConstraints deduced(Key key, Role role)
{
    ResolvedConstraints out{};
    out.key = {key, Source::deduced};
    out.role = {role, Source::deduced};
    out.resolution = {Resolution::eighth, Source::deduced};
    out.density = {Density::medium, Source::defaulted};
    out.reg = {Register::mid, Source::deduced};
    out.form = {Form::aaba, Source::deduced};
    return out;
}

} // namespace

TEST_CASE("phrase: the length, the role and the key, and nothing the person did not ask for")
{
    const auto constraints = deduced(Key{9, Mode::minor}, Role::melody);
    CHECK(phrase({constraints, 16.0, 4.0, 0.0}) == "quatre mesures de mélodie en la mineur");
}

TEST_CASE("phrase: the style is said from a quarter learned")
{
    const auto constraints = deduced(Key{9, Mode::minor}, Role::melody);
    CHECK(phrase({constraints, 16.0, 4.0, 0.24}) == "quatre mesures de mélodie en la mineur");
    CHECK(phrase({constraints, 16.0, 4.0, 0.25}) == "quatre mesures de mélodie en la mineur, dans ton style");
}

TEST_CASE("phrase: keys are said, not written")
{
    CHECK(phrase({deduced(Key{6, Mode::minor}, Role::chords), 8.0, 4.0, 0.0}) ==
          "deux mesures d'accords en fa dièse mineur");
    CHECK(phrase({deduced(Key{10, Mode::major}, Role::bass), 4.0, 4.0, 0.0}) ==
          "une mesure de basse en si bémol majeur");
    CHECK(phrase({deduced(Key{2, Mode::major}, Role::bass), 4.0, 4.0, 0.0}) ==
          "une mesure de basse en ré majeur");
}

TEST_CASE("phrase: a rhythm channel has no key")
{
    CHECK(phrase({deduced(Key{9, Mode::minor}, Role::rhythm), 16.0, 4.0, 0.0}) == "quatre mesures de rythme");
}

TEST_CASE("phrase: what was imposed is said in words, the form without its letters")
{
    auto constraints = deduced(Key{9, Mode::minor}, Role::melody);
    constraints.resolution = {Resolution::sixteenth, Source::imposed};
    constraints.reg = {Register::low, Source::imposed};
    constraints.density = {Density::sparse, Source::imposed};
    constraints.form = {Form::loop, Source::imposed};
    const auto said = phrase({constraints, 16.0, 4.0, 0.0});
    CHECK(said ==
          "quatre mesures de mélodie en la mineur, en doubles-croches, dans le grave, aérée, en boucle");
    CHECK(said.find("AABA") == std::string::npos);

    constraints.form = {Form::aaba, Source::imposed};
    CHECK(phrase({constraints, 16.0, 4.0, 0.0}).find("AABA") == std::string::npos);
}

TEST_CASE("phrase: lengths that are not whole bars")
{
    CHECK(lengthWords(6.0, 4.0) == "six temps");
    CHECK(lengthWords(1.0, 4.0) == "un temps");
    CHECK(lengthWords(96.0, 4.0) == "24 mesures");
    CHECK(lengthWords(1.5, 4.0) == "un fragment");
    CHECK(lengthWords(12.0, 3.0) == "quatre mesures");
}
