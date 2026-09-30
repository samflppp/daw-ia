#include "daw/domain/Value.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/model/TokenTable.h"

#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

#include <doctest/doctest.h>

using daw::ui::TokenTable;
using Kind = TokenTable::Entry::Kind;

namespace
{

std::string shippedTokens()
{
    std::ifstream file{DAW_TOKENS_FILE};
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// What Tokens::colour answered before S18 bis, written again the way it was
// written then: "ff" put in front of six digits, the last two of eight moved
// to the front, the whole read as one hexadecimal number.
std::uint32_t colourAsBefore(std::string value)
{
    const auto hex = value.substr(1);
    const auto argb = hex.size() == 6 ? "ff" + hex : hex.substr(6) + hex.substr(0, 6);
    return static_cast<std::uint32_t>(std::stoul(argb, nullptr, 16));
}

} // namespace

TEST_CASE("every token of tokens.json gives the value it gave before")
{
    const auto text = shippedTokens();
    REQUIRE(!text.empty());

    const auto table = TokenTable::fromJson(text);
    REQUIRE(table.valid());

    const auto root = daw::domain::json::read(text);
    REQUIRE(root.ok());

    int colours = 0;
    int numbers = 0;
    std::function<void(const daw::domain::Value&, const std::string&)> walk =
        [&](const daw::domain::Value& node, const std::string& path)
    {
        const auto* entry = path.empty() ? nullptr : table.find(path);
        if (!path.empty())
            REQUIRE_MESSAGE(entry != nullptr, path);

        if (const auto* members = node.asObject(); members != nullptr)
        {
            if (entry != nullptr)
                CHECK_MESSAGE(entry->kind == Kind::group, path);
            for (const auto& [key, child] : *members)
                walk(child, path.empty() ? key : path + "." + key);
            return;
        }

        if (node.isInt() || node.isNumber())
        {
            ++numbers;
            CHECK_MESSAGE(entry->kind == Kind::number, path);
            CHECK_MESSAGE(static_cast<float>(entry->number) == static_cast<float>(node.asDouble().value()),
                          path);
            return;
        }

        const auto value = node.asString().value();
        if (value.starts_with('#'))
        {
            ++colours;
            CHECK_MESSAGE(entry->kind == Kind::colour, path);
            CHECK_MESSAGE(entry->argb == colourAsBefore(value), path);
        }
        else
        {
            CHECK_MESSAGE(entry->kind == Kind::text, path);
        }
    };
    walk(root.value(), {});

    // The file as shipped: a table that found nothing would pass the walk.
    CHECK(colours > 30);
    CHECK(numbers > 60);
}

TEST_CASE("a token is decoded once, alpha last in the file and first in the value")
{
    const auto table = TokenTable::fromJson(R"({"a": {"opaque": "#0B0B0C", "veiled": " #18181BF2 ",
                                                      "size": 12, "tracking": -0.005, "family": "Inter",
                                                      "broken": "#12345"}})");
    REQUIRE(table.valid());

    CHECK(table.find("a.opaque")->argb == 0xff0b0b0cU);
    CHECK(table.find("a.veiled")->argb == 0xf218181bU);
    CHECK(table.find("a.size")->number == 12.0);
    CHECK(table.find("a.tracking")->number == -0.005);
    CHECK(table.find("a.family")->kind == Kind::text);
    CHECK(table.find("a.broken")->kind == Kind::invalid);
    CHECK(table.find("a")->kind == Kind::group);
}

TEST_CASE("an unknown token is not found, and malformed tokens are not a table")
{
    const auto table = TokenTable::fromJson(R"({"space": {"md": 12}})");
    CHECK(table.find("space.mdd") == nullptr);
    CHECK(table.find("space.md.x") == nullptr);
    CHECK(table.find("") == nullptr);

    CHECK(!TokenTable::fromJson("[1, 2]").valid());
    CHECK(!TokenTable::fromJson("{ not json").valid());
}
