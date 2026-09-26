#include "daw/ui/model/SampleSearch.h"

#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::ui;

namespace
{

SearchEntry entry(const std::string& folders, const std::string& name)
{
    return {folders + "/" + name + ".wav", searchWords(name), searchWords(folders)};
}

std::vector<std::string> found(const std::vector<SearchEntry>& entries, const std::string& query)
{
    std::vector<std::string> names;
    for (const auto& hit : searchSamples(entries, query, 50))
        names.push_back(entries[hit.entry].path);
    return names;
}

} // namespace

TEST_CASE("a name is cut into lowercase words, letters apart from digits")
{
    CHECK(searchWords("Kick_House-01") == std::vector<std::string>{"kick", "house", "01"});
    CHECK(searchWords("Kick808 Hard") == std::vector<std::string>{"kick", "808", "hard"});
    CHECK(searchWords("  ") == std::vector<std::string>{});
    CHECK(searchWords("Café Snare") == std::vector<std::string>{"caf\xc3\xa9", "snare"});
}

TEST_CASE("a query word meets a name word: same, prefix, typo, family, nothing")
{
    CHECK(wordMatch("kick", "kick") == 1.0);
    CHECK(wordMatch("kick", "kicks") > wordMatch("kcik", "kick"));
    CHECK(wordMatch("hous", "house") > 0.8);
    CHECK(wordMatch("kcik", "kick") > wordMatch("house", "club"));
    CHECK(wordMatch("house", "club") > 0.0);
    CHECK(wordMatch("house", "techno") > 0.0);
    CHECK(wordMatch("kick", "bd") > 0.0);
    CHECK(wordMatch("kick", "snare") == 0.0);

    // Too short to guess from: "h" starts "hat" and "house" alike.
    CHECK(wordMatch("h", "house") == 0.0);
    CHECK(wordMatch("hat", "hit") == 0.0);
}

TEST_CASE("kick house: the house kicks first, then the club and techno ones, then half matches")
{
    const std::vector<SearchEntry> entries{
        entry("Pack", "Snare House"),
        entry("Pack", "Kick Club 01"),
        entry("Pack", "Kick House"),
        entry("Pack", "Hat Trap"),
        entry("Pack", "Kick Techno"),
        entry("Pack", "Kick 808"),
    };

    const auto names = found(entries, "kick house");
    REQUIRE(names.size() == 5);
    CHECK(names[0] == "Pack/Kick House.wav");
    // Same score for both styles next to house: the shorter name first.
    CHECK(names[1] == "Pack/Kick Techno.wav");
    CHECK(names[2] == "Pack/Kick Club 01.wav");
    // Half the query each: the shorter name first, then by path.
    CHECK(names[3] == "Pack/Kick 808.wav");
    CHECK(names[4] == "Pack/Snare House.wav");
}

TEST_CASE("the folders speak for the files in them")
{
    const std::vector<SearchEntry> entries{
        entry("Club Pack/Kicks", "01"),
        entry("Club Pack/Snares", "01"),
        entry("Trap Pack/Kicks", "01"),
    };

    const auto names = found(entries, "kick house");
    REQUIRE(names.size() == 3);
    CHECK(names[0] == "Club Pack/Kicks/01.wav");
}

TEST_CASE("typos and a missing letter still find the sample")
{
    const std::vector<SearchEntry> entries{entry("Pack", "Clap Tight"), entry("Pack", "Snare Tight")};

    CHECK(found(entries, "calp").front() == "Pack/Clap Tight.wav");
    CHECK(found(entries, "snre").front() == "Pack/Snare Tight.wav");
    CHECK(found(entries, "").empty());
    CHECK(found(entries, "violon").empty());
}
