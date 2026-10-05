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

TEST_CASE("Direction: the sections stand on the project's grid, in bars of the reference")
{
    // 120 BPM: a bar of the reference is 2 s. The last section is cut short
    // by the end of the file, 61.3 s, and rounds to its nearest bar.
    auto read = reference("a.wav", 120.0, aMinor);
    read.reading.sections.clear();
    const auto section = [](double from, double to, char label)
    {
        direction::Section out;
        out.fromSeconds = from;
        out.toSeconds = to;
        out.label = label;
        return out;
    };
    read.reading.sections = {section(0.0, 16.0, 'A'), section(16.0, 48.0, 'B'), section(48.0, 61.3, 'A')};

    direction::Direction wanted;
    wanted.references.push_back(read);
    const auto placed = direction::onGrid(direction::combine(wanted));
    REQUIRE(placed.size() == 3);
    CHECK(placed[0] == direction::GridSection{0.0, 32.0, 'A'});
    CHECK(placed[1] == direction::GridSection{32.0, 96.0, 'B'});
    CHECK(placed[2] == direction::GridSection{96.0, 124.0, 'A'});

    // In 4/4: bars 1 to 8, 9 to 24, 25 to 31, as the ruler counts them.
    CHECK(direction::firstBar(placed[0], 4.0) == 1);
    CHECK(direction::lastBar(placed[0], 4.0) == 8);
    CHECK(direction::firstBar(placed[1], 4.0) == 9);
    CHECK(direction::lastBar(placed[1], 4.0) == 24);
    CHECK(direction::firstBar(placed[2], 4.0) == 25);
    CHECK(direction::lastBar(placed[2], 4.0) == 31);

    // In 3/4 a beat is still a beat: 32 beats reach into the eleventh bar.
    CHECK(direction::lastBar(placed[0], 3.0) == 11);
    CHECK(direction::firstBar(placed[1], 3.0) == 11);
}

TEST_CASE("Direction: the grid is the tempo the sections were cut at, not a correction")
{
    // 92 BPM: a bar is 60/92*4 s. Four bars, then twelve.
    const auto bar = 4.0 * 60.0 / 92.0;
    auto read = reference("a.wav", 92.0, aMinor);
    read.reading.sections.clear();
    direction::Section first;
    first.toSeconds = 4 * bar;
    direction::Section second;
    second.fromSeconds = 4 * bar;
    second.toSeconds = 16 * bar;
    second.label = 'B';
    read.reading.sections = {first, second};

    direction::Direction wanted;
    wanted.references.push_back(read);
    const auto placed = direction::onGrid(direction::combine(wanted));
    REQUIRE(placed.size() == 2);
    CHECK(placed[1] == direction::GridSection{16.0, 64.0, 'B'});

    // A tempo typed by hand moves the project's pulse, not the reference's
    // cuts.
    wanted.corrections.bpm = 140.0;
    CHECK(direction::onGrid(direction::combine(wanted)) == placed);

    // Without a tempo the reading cut two-second blocks: no bar to stand on.
    wanted.references.front().reading.bpm.reset();
    CHECK(direction::onGrid(direction::combine(wanted)).empty());
}
