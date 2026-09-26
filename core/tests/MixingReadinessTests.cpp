#include "TestSupport.h"
#include "daw/domain/copilot/MixingReadiness.h"

#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;

namespace
{

// What the copilot is offered today: every command of the registry, and the
// methods the application answers.
std::vector<std::string> offeredToday()
{
    auto offered = CommandRegistry::withBuiltinCommands().types();
    for (const auto* method : {"state.get", "clip.notes", "plugins.find", "mix.levels", "commands.execute"})
        offered.emplace_back(method);
    return offered;
}

} // namespace

TEST_CASE("The verbs of the mix are all there, the measures to judge it are not yet")
{
    const auto gaps = copilot::mixingGaps(offeredToday());

    std::vector<std::string> missing;
    for (const auto& gap : gaps)
        missing.push_back(gap.satisfiedBy.front());

    // Pinned: a change to this list is a change to what the copilot can do,
    // and it is said in a review, not discovered.
    CHECK(missing == std::vector<std::string>{"plugin.parameters",
                                              "mix.stock_effects",
                                              "mix.measure",
                                              "mix.loudness",
                                              "mix.spectrum",
                                              "mix.masking",
                                              "mix.stereo",
                                              "mix.dynamics",
                                              "track.set_sidechain",
                                              "mix.reference"});

    for (const auto& gap : gaps)
    {
        CHECK_FALSE(gap.what.empty());
        CHECK_FALSE(gap.why.empty());
    }
}

TEST_CASE("A need is met by any one of the names that satisfy it")
{
    auto offered = offeredToday();
    const auto before = copilot::mixingGaps(offered).size();
    offered.emplace_back("mix.measure");
    CHECK(copilot::mixingGaps(offered).size() == before - 1);
    CHECK(copilot::mixingGaps({}).size() == copilot::mixingNeeds().size());
}
