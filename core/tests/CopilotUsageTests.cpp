#include "daw/domain/copilot/Usage.h"
#include "daw/domain/serialization/Json.h"

#include <doctest/doctest.h>

using namespace daw::domain;

namespace
{

// The `usage` of an answer, written the way the service writes it:
// Usage.as_dict() in services/src/daw_services/ia_provider, which
// services/tests/test_mixing.py pins on its side.
constexpr const char* fromTheService =
    R"({"inputTokens": 5200, "outputTokens": 900, "cacheReadTokens": 1200, "cacheWriteTokens": 300})";

copilot::Usage read(const char* text)
{
    const auto value = json::read(text);
    REQUIRE(value.ok());
    return copilot::Usage::fromValue(value.value());
}

} // namespace

TEST_CASE("The tokens of an answer are read under the names the service sends")
{
    const auto usage = read(fromTheService);

    CHECK(usage.inputTokens == 5200);
    CHECK(usage.outputTokens == 900);
    CHECK(usage.cacheReadTokens == 1200);
    CHECK(usage.cacheWriteTokens == 300);
}

TEST_CASE("Tokens under the names of the S20 bug read zero, not a guess")
{
    // What the mix read before S21: the provider's own names, never sent.
    const auto usage = read(R"({"input_tokens": 5200, "output_tokens": 900})");

    CHECK(usage == copilot::Usage{});
}

TEST_CASE("Two rounds of a decision add their tokens, and the sum reads back")
{
    const auto first = read(fromTheService);
    const auto second = read(R"({"inputTokens": 2480, "outputTokens": 4922})");

    const auto total = first.plus(second);
    CHECK(total.inputTokens == 7680);
    CHECK(total.outputTokens == 5822);
    CHECK(total.cacheReadTokens == 1200);

    CHECK(copilot::Usage::fromValue(total.toValue()) == total);
}
