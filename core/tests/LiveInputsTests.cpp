#include "daw/domain/live/Inputs.h"

#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain::live;

TEST_CASE("MIDI inputs: a keyboard plugged in takes the first free source")
{
    const auto change = follow({}, {"arturia"}, 1, 6);
    REQUIRE(change.added.size() == 1);
    CHECK(change.added[0] == InputSlot{"arturia", 1});
    CHECK(change.removed.empty());
}

TEST_CASE("MIDI inputs: unplugged, it is released, and plugged back it plays again")
{
    const std::vector<InputSlot> listened{{"arturia", 1}, {"pads", 2}};

    const auto unplugged = follow(listened, {"pads"}, 1, 6);
    REQUIRE(unplugged.removed.size() == 1);
    CHECK(unplugged.removed[0] == InputSlot{"arturia", 1});
    REQUIRE(unplugged.kept.size() == 1);
    CHECK(unplugged.kept[0] == InputSlot{"pads", 2}); // the other keeps its source

    const auto back = follow(unplugged.kept, {"pads", "arturia"}, 1, 6);
    REQUIRE(back.added.size() == 1);
    CHECK(back.added[0] == InputSlot{"arturia", 1}); // the free source again
    CHECK(back.removed.empty());
}

TEST_CASE("MIDI inputs: past the sources the router keeps, a keyboard waits")
{
    const auto change = follow({}, {"a", "b", "c"}, 1, 2);
    CHECK(change.added.size() == 2);
    CHECK(change.waiting == std::vector<std::string>{"c"});
}
