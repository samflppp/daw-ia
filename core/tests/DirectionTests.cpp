#include "TestSupport.h"
#include "daw/domain/commands/DirectionCommands.h"
#include "daw/domain/direction/Direction.h"
#include "daw/domain/serialization/Json.h"

#include <memory>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;

namespace
{

direction::Reference
reference(const std::string& name, std::optional<double> bpm, std::optional<generation::Key> key)
{
    direction::Reference out;
    out.reading.name = name;
    out.reading.digest = std::string(64, name.front() == 'a' ? 'a' : 'b');
    out.reading.seconds = 180.0;
    out.reading.bpm = bpm;
    out.reading.key = key;
    out.reading.crestDb = 10.0;
    out.reading.sideShare = 0.2;
    out.reading.tilt.fill(0.0);
    out.reading.stems["vocals"] = direction::StemReading{-14.0, -4.0, 0.5};
    direction::Section section;
    section.toSeconds = 180.0;
    out.reading.sections.push_back(section);
    return out;
}

constexpr generation::Key aMinor{9, generation::Mode::minor};
constexpr generation::Key eMajor{4, generation::Mode::major};

} // namespace

TEST_CASE("Direction: a project without one serialises as before S22, byte for byte")
{
    daw::testing::Harness harness;
    const auto text = json::write(harness.state.toValue());
    CHECK(text.find("direction") == std::string::npos);
    CHECK(harness.state.direction().empty());
}

TEST_CASE("Direction: direction.set is one Ctrl+Z, to the byte, and one Ctrl+Y")
{
    daw::testing::Harness harness;
    const auto before = json::write(harness.state.toValue());

    direction::Direction wanted;
    wanted.references.push_back(reference("a.wav", 120.0, aMinor));
    wanted.amount = 0.7;
    REQUIRE(harness.bus.execute(std::make_unique<SetDirection>(wanted)).ok());
    CHECK(harness.state.direction() == wanted);
    CHECK(json::write(harness.state.toValue()).find("\"direction\"") != std::string::npos);

    REQUIRE(harness.bus.undo().ok());
    CHECK(json::write(harness.state.toValue()) == before);
    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.state.direction() == wanted);
}

TEST_CASE("Direction: the state reads back the direction it wrote")
{
    daw::testing::Harness harness;
    direction::Direction wanted;
    wanted.references.push_back(reference("a.wav", 120.0, aMinor));
    wanted.references.push_back(reference("b.wav", std::nullopt, std::nullopt));
    wanted.references.back().weight = 2.0;
    wanted.corrections.bpm = 118.0;
    REQUIRE(harness.bus.execute(std::make_unique<SetDirection>(wanted)).ok());

    const auto back = ProjectState::fromValue(harness.state.toValue());
    REQUIRE(back.ok());
    CHECK(back.value().direction() == wanted);
}

TEST_CASE("Direction: the command replays from its payload, as the journal does")
{
    direction::Direction wanted;
    wanted.references.push_back(reference("a.wav", 120.0, aMinor));
    const SetDirection command{wanted};
    const auto registry = CommandRegistry::withBuiltinCommands();
    auto replayed = registry.create(std::string{SetDirection::commandType}, command.payload());
    REQUIRE(replayed.ok());
    CHECK(json::write(replayed.value()->payload()) == json::write(command.payload()));
}

TEST_CASE("Direction: refused when its numbers make no sense")
{
    direction::Direction wanted;
    wanted.amount = 1.5;
    CHECK(direction::Direction::fromValue(wanted.toValue()).code() == ErrorCode::invalidArgument);

    wanted.amount = 0.5;
    wanted.references.push_back(reference("a.wav", 120.0, aMinor));
    wanted.references.front().weight = 0.0;
    CHECK(direction::Direction::fromValue(wanted.toValue()).code() == ErrorCode::invalidArgument);
}

TEST_CASE("Direction: references that agree give their tempo and key")
{
    direction::Direction both;
    both.references.push_back(reference("a.wav", 120.0, aMinor));
    both.references.push_back(reference("b.wav", 122.0, aMinor));
    both.references.back().weight = 3.0;
    const auto combined = direction::combine(both);
    REQUIRE(combined.bpm.has_value());
    CHECK(*combined.bpm == doctest::Approx((120.0 + 3.0 * 122.0) / 4.0));
    CHECK(combined.key == aMinor);
    CHECK(combined.contradictions.empty());
}

TEST_CASE("Direction: two tempos or two keys are said, never averaged")
{
    direction::Direction both;
    both.references.push_back(reference("a.wav", 120.0, aMinor));
    both.references.push_back(reference("b.wav", 92.0, eMajor));
    const auto combined = direction::combine(both);
    CHECK_FALSE(combined.bpm.has_value());
    CHECK_FALSE(combined.key.has_value());
    REQUIRE(combined.contradictions.size() == 2);
    CHECK(combined.contradictions[0].find("120 BPM (a.wav)") != std::string::npos);
    CHECK(combined.contradictions[0].find("92 BPM (b.wav)") != std::string::npos);
    CHECK(combined.contradictions[1].find("La mineur") != std::string::npos);

    // The person chooses: the correction holds, the contradiction is gone.
    both.corrections.bpm = 92.0;
    both.corrections.key = eMajor;
    const auto chosen = direction::combine(both);
    CHECK(chosen.bpm == 92.0);
    CHECK(chosen.bpmCorrected);
    CHECK(chosen.key == eMajor);
    CHECK(chosen.contradictions.empty());
}

TEST_CASE("Direction: a key no reference settles offers the candidates")
{
    direction::Direction one;
    auto unsure = reference("a.wav", 120.0, std::nullopt);
    unsure.reading.keyCandidates = {aMinor, generation::Key{0, generation::Mode::major}};
    one.references.push_back(unsure);
    const auto combined = direction::combine(one);
    CHECK_FALSE(combined.key.has_value());
    CHECK(combined.keyCandidates.size() == 2);
}

TEST_CASE("Direction: continuous values are weighted means, the sections the heaviest reference's")
{
    direction::Direction both;
    both.references.push_back(reference("a.wav", 120.0, aMinor));
    both.references.push_back(reference("b.wav", 120.0, aMinor));
    both.references[1].weight = 3.0;
    both.references[1].reading.crestDb = 14.0;
    both.references[1].reading.tilt[0] = 4.0;
    both.references[1].reading.stems["vocals"].balanceDb = -8.0;
    both.references[1].reading.sections.push_back(direction::Section{});
    const auto combined = direction::combine(both);
    CHECK(*combined.crestDb == doctest::Approx(13.0));
    CHECK((*combined.tilt)[0] == doctest::Approx(3.0));
    CHECK(combined.balanceDb.at("vocals") == doctest::Approx(-7.0));
    CHECK(combined.sections.size() == 2);
}
