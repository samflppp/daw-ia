#include "daw/domain/rights/Rights.h"

#include <doctest/doctest.h>

using namespace daw::domain::rights;

TEST_CASE("the prototype allows every feature, and a check can refuse them all")
{
    const Feature all[] = {Feature::copilot,
                           Feature::mixByModel,
                           Feature::mix,
                           Feature::generation,
                           Feature::stems,
                           Feature::voice,
                           Feature::kit,
                           Feature::buses,
                           Feature::direction};
    for (const auto feature : all)
        CHECK(allows(feature));

    refuseAllForCheck(true);
    for (const auto feature : all)
        CHECK_FALSE(allows(feature));
    refuseAllForCheck(false);
    for (const auto feature : all)
        CHECK(allows(feature));
}

TEST_CASE("what goes through the API and what runs on the machine")
{
    CHECK(routeOf(Feature::copilot) == Route::api);
    CHECK(routeOf(Feature::mixByModel) == Route::api);
    CHECK(routeOf(Feature::stems) == Route::local);
    CHECK(routeOf(Feature::voice) == Route::local);
    CHECK(refusal(Feature::stems) == "Ta licence ne comprend pas la séparation en stems : rien n'a changé.");
}
