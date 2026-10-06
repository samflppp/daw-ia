#include "daw/domain/live/AudioAdvice.h"
#include "daw/domain/serialization/Json.h"

#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain::live;

// The « Audio » window's advice (S24), measured on the card. The lists are
// those JUCE gives: shared mode one size, Low Latency Mode the multiples of
// the period, exclusive mode its sizes; the trials those of the founder's
// card on 6 October 2026, where the exclusive mode stalls up to 75 ms.

namespace
{

const std::string shared{sharedDriver};
const std::string low{lowLatencyDriver};
const std::string exclusive{exclusiveDriver};

CardTrial trial(const std::string& type, int buffer, double worstMs, std::int64_t late, double outputMs)
{
    CardTrial out;
    out.type = type;
    out.buffer = buffer;
    out.sampleRate = 48000.0;
    out.outputSeconds = outputMs / 1000.0;
    out.timing.blocks = 600;
    out.timing.lastSize = buffer;
    out.timing.meanSeconds = buffer / 48000.0;
    out.timing.worstSeconds = worstMs / 1000.0;
    out.timing.late = late;
    return out;
}

// The founder's Realtek, measured.
std::vector<CardTrial> realtek()
{
    return {trial(shared, 480, 10.57, 0, 10.0),
            trial(low, 480, 10.70, 0, 10.0),
            trial(exclusive, 144, 75.68, 46, 6.0),
            trial(exclusive, 192, 75.59, 47, 8.0),
            trial(exclusive, 256, 75.81, 46, 10.67),
            trial(exclusive, 480, 75.76, 46, 20.0)};
}

} // namespace

TEST_CASE("The trial opens the shared buffer, Low Latency up to 480, and the exclusive sizes offered")
{
    const auto setups = trialSetups(
        {{shared, {480}}, {low, {96, 192, 288, 384, 480, 576}}, {exclusive, {144, 160, 192, 256, 480, 960}}});
    CHECK(setups.size() == 10);
    CHECK(setups.front().type == shared);
    CHECK(setups[1].buffer == 96);
    CHECK(setups[5].buffer == 480);
    CHECK(setups[6].type == exclusive);
    CHECK(setups[6].buffer == 144);
    CHECK(setups.back().buffer == 480);
}

TEST_CASE("A card offering none of the exclusive sizes tried is tried at its smallest")
{
    const auto setups = trialSetups({{shared, {441}}, {exclusive, {441, 882}}});
    REQUIRE(setups.size() == 2);
    CHECK(setups.back().buffer == 441);
}

TEST_CASE("On the founder's card, the shared mode is advised: the exclusive one drops out at every size")
{
    const auto advice = advise(realtek(), exclusive, 256);
    CHECK(advice.measured);
    CHECK(advice.type != exclusive);
    CHECK(advice.buffer == 480);
    CHECK_FALSE(advice.silencesOthers);
    CHECK(advice.sentence.find("sans un décrochage") != std::string::npos);
    CHECK(advice.sentence.find("4 décrochent") != std::string::npos);
}

TEST_CASE("Among setups that hold, the one that waits least at worst, 256 samples or less preferred")
{
    const auto advice = advise({trial(shared, 480, 10.5, 0, 10.0),
                                trial(exclusive, 144, 3.4, 0, 6.0),
                                trial(exclusive, 256, 5.6, 0, 10.7),
                                trial(exclusive, 480, 10.2, 0, 20.0)},
                               shared,
                               480);
    CHECK(advice.type == exclusive);
    CHECK(advice.buffer == 144);
    CHECK(advice.silencesOthers);
    CHECK(advice.sentence.find("se taisent") != std::string::npos);
}

TEST_CASE("A setup that waits less but drops one block is never advised")
{
    const auto advice =
        advise({trial(shared, 480, 10.5, 0, 10.0), trial(exclusive, 144, 3.4, 1, 6.0)}, shared, 480);
    CHECK(advice.type == shared);
    CHECK(advice.already);
    CHECK(advice.sentence.rfind("Réglage conseillé, déjà en place.", 0) == 0);
}

TEST_CASE("Nothing held, nothing advised, and nothing tried, nothing advised")
{
    const auto dropped = advise({trial(exclusive, 144, 75.0, 40, 6.0)}, shared, 480);
    CHECK(dropped.already);
    CHECK(dropped.type == shared);
    CHECK(dropped.sentence.find("Aucun réglage n'a tenu") != std::string::npos);

    const auto untried = advise({}, shared, 480);
    CHECK(untried.already);
    CHECK_FALSE(untried.measured);
    CHECK(untried.sentence.find("Tester ma carte") != std::string::npos);
}

TEST_CASE("The worst wait counts a block, its margin, the slowest block past it, and the card")
{
    // 480 samples at 48 kHz: 10 ms, margin 2.5 ms; a block 3 ms late past
    // the margin; 10 ms declared.
    CHECK(worstKeyToEar(trial(shared, 480, 15.5, 0, 10.0)) == doctest::Approx(0.0255));
    // A regular card: no overshoot.
    CHECK(worstKeyToEar(trial(shared, 480, 10.5, 0, 10.0)) == doctest::Approx(0.0225));
}

TEST_CASE("Trials go to JSON and back unchanged")
{
    const auto trials = realtek();
    const auto text = daw::domain::json::write(toValue(trials));
    const auto read = daw::domain::json::read(text);
    REQUIRE(read.ok());
    const auto back = trialsFromValue(read.value());
    REQUIRE(back.size() == trials.size());
    CHECK(daw::domain::json::write(toValue(back)) == text);
    CHECK(back[2].timing.late == 46);
}

TEST_CASE("The closest buffer is kept when the driver changes, the smaller on a tie")
{
    CHECK(closestBuffer({96, 192, 288, 384, 480}, 256) == 288);
    CHECK(closestBuffer({128, 384}, 256) == 128);
    CHECK(closestBuffer({480}, 128) == 480);
    CHECK(closestBuffer({}, 256) == 0);
}

TEST_CASE("A block is late past one and a half blocks, never before")
{
    const auto block = 480.0 / 48000.0;
    CHECK_FALSE(isLate(block, block));
    CHECK_FALSE(isLate(1.49 * block, block));
    CHECK(isLate(1.51 * block, block));
    CHECK_FALSE(isLate(1.0, 0.0));
}

TEST_CASE("The latency reads from the key to the ear, the card's share said apart")
{
    const auto line = describeLatency(0.0125, true, 0.010);
    CHECK(line.find("12,5 ms (mesuré)") != std::string::npos);
    CHECK(line.find("la carte déclare 10,0 ms") != std::string::npos);
    CHECK(line.find("de la touche à l'oreille : 22,5 ms") != std::string::npos);
}
