#include "daw/ui/model/PromptReader.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw;
using namespace daw::ui;
using domain::generation::Interpretation;
using domain::generation::Role;

namespace
{

// A copilot held in the hand: it answers when the test says so.
struct Copilot
{
    bool there{true};
    std::vector<std::uint64_t> asked;
    std::vector<std::string> texts;
    RoutedPromptReader::Answered answer;

    RoutedPromptReader::Remote remote()
    {
        return {[this] { return there; },
                [this](std::uint64_t ticket,
                       const std::string& text,
                       const PromptReader::Zone&,
                       RoutedPromptReader::Answered answered)
                {
                    asked.push_back(ticket);
                    texts.push_back(text);
                    answer = std::move(answered);
                }};
    }
};

PromptReader::Reading chords()
{
    PromptReader::Reading out{};
    out.interpretation.constraints.role = Role::chords;
    return out;
}

} // namespace

TEST_CASE("prompt reading: the copilot's reading is the answer, and nothing is said")
{
    Copilot copilot;
    RoutedPromptReader reader{copilot.remote()};
    std::optional<PromptReader::Reading> got;

    reader.read("des accords tristes", {}, [&](PromptReader::Reading reading) { got = std::move(reading); });
    CHECK(reader.reading());
    CHECK_FALSE(got.has_value());
    REQUIRE(copilot.asked.size() == 1);
    CHECK(copilot.texts.front() == "des accords tristes");

    copilot.answer(copilot.asked.front(), chords());
    REQUIRE(got.has_value());
    CHECK(got->remote);
    CHECK(got->notice.empty());
    CHECK(got->interpretation.constraints.role == Role::chords);
    CHECK_FALSE(reader.reading());
}

TEST_CASE("prompt reading: no copilot, the local words answer at once, and the window says so")
{
    Copilot copilot;
    copilot.there = false;
    RoutedPromptReader reader{copilot.remote()};
    std::optional<PromptReader::Reading> got;

    reader.read("Am basse", {}, [&](PromptReader::Reading reading) { got = std::move(reading); });
    CHECK(copilot.asked.empty());
    REQUIRE(got.has_value());
    CHECK_FALSE(got->remote);
    CHECK(got->notice == RoutedPromptReader::offlineNotice);
    CHECK(got->interpretation.constraints.role == Role::bass);
}

TEST_CASE("prompt reading: a failure falls back on the local words")
{
    Copilot copilot;
    RoutedPromptReader reader{copilot.remote()};
    std::optional<PromptReader::Reading> got;

    reader.read("Am basse", {}, [&](PromptReader::Reading reading) { got = std::move(reading); });
    copilot.answer(copilot.asked.front(), domain::fail(domain::ErrorCode::conflict, "no key"));
    REQUIRE(got.has_value());
    CHECK_FALSE(got->remote);
    CHECK(got->notice == RoutedPromptReader::failedNotice);
    CHECK(got->interpretation.constraints.role == Role::bass);
}

TEST_CASE("prompt reading: too slow, the local words answer, and the late answer is dropped")
{
    Copilot copilot;
    RoutedPromptReader reader{copilot.remote()};
    int calls = 0;
    std::optional<PromptReader::Reading> got;

    reader.read("Am basse", {}, [&](PromptReader::Reading reading) { ++calls, got = std::move(reading); });
    reader.expire();
    REQUIRE(got.has_value());
    CHECK(got->notice == RoutedPromptReader::slowNotice);

    copilot.answer(copilot.asked.front(), chords());
    CHECK(calls == 1);
    CHECK(got->interpretation.constraints.role == Role::bass);

    // Nothing waits any more: a second expire does nothing.
    reader.expire();
    CHECK(calls == 1);
}

TEST_CASE("prompt reading: a second prompt replaces the first, a cancelled one never answers")
{
    Copilot copilot;
    RoutedPromptReader reader{copilot.remote()};
    std::vector<std::string> answers;
    const auto record = [&](PromptReader::Reading reading)
    { answers.push_back(reading.remote ? "remote" : reading.notice); };

    reader.read("premier", {}, record);
    reader.read("second", {}, record);
    REQUIRE(copilot.asked.size() == 2);

    copilot.answer(copilot.asked[0], chords());
    CHECK(answers.empty());
    copilot.answer(copilot.asked[1], chords());
    CHECK(answers == std::vector<std::string>{"remote"});

    reader.read("troisième", {}, record);
    reader.cancel();
    copilot.answer(copilot.asked[2], chords());
    reader.expire();
    CHECK(answers.size() == 1);
}

namespace
{

PromptReader::Zone withNotes(bool hasNotes)
{
    PromptReader::Zone zone{};
    zone.hasNotes = hasNotes;
    return zone;
}

} // namespace

TEST_CASE("prompt reading: the local words that ask to rework notes are used, not ignored")
{
    const auto reading = LocalPromptReader::parse("plus sombre en croches", withNotes(true));
    REQUIRE(reading.transform.has_value());
    CHECK(*reading.transform == domain::generation::Transform::darker);
    // "en" is a filler the S14 words report; "plus" and "sombre" were used.
    const auto& ignored = reading.interpretation.ignored;
    CHECK(std::find(ignored.begin(), ignored.end(), "plus") == ignored.end());
    CHECK(std::find(ignored.begin(), ignored.end(), "sombre") == ignored.end());
    CHECK(reading.interpretation.constraints.resolution == domain::generation::Resolution::eighth);

    CHECK_FALSE(LocalPromptReader::parse("Am basse", withNotes(true)).transform.has_value());
}

TEST_CASE("prompt reading: over an empty zone, a word to rework notes is said ignored, not lost")
{
    // Nothing to rework: "sombre" is no constraint of a new proposal. Before
    // S22 it left the ignored words all the same, and vanished unsaid.
    const auto reading = LocalPromptReader::parse("Am doubles dense grave basse sombre", withNotes(false));
    CHECK_FALSE(reading.transform.has_value());
    const auto& ignored = reading.interpretation.ignored;
    CHECK(std::find(ignored.begin(), ignored.end(), "sombre") != ignored.end());
    CHECK(reading.interpretation.constraints.role == Role::bass);
}
