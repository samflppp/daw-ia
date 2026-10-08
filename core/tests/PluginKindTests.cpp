#include "TestSupport.h"
#include "daw/domain/buses/Kind.h"
#include "daw/domain/buses/Shared.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TrackCommands.h"

#include <map>
#include <memory>
#include <optional>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using buses::Kind;
using buses::KnownBy;
using daw::testing::Harness;

// What a plugin of the person's is, for the smart buses (decided on
// 8 October 2026): the person's answer, the catalogue, a sure word of the
// name, otherwise a question. The names and categories are the founder's
// catalogue, as the machine scanned it.

TEST_CASE("The catalogue is believed, « Fx » alone says nothing")
{
    CHECK((buses::recognise("TR5 CSR Hall", "Fx|Reverb", false, std::nullopt).kind == Kind::reverb));
    CHECK((buses::recognise("UAD Roland RE-201", "Fx|Delay", false, std::nullopt).kind == Kind::delay));
    CHECK((buses::recognise("ValhallaSpaceModulator", "Fx|Modulation", false, std::nullopt).kind ==
           Kind::other));
    CHECK((buses::recognise("Serum", "Instrument|Synth", true, std::nullopt).kind == Kind::instrument));

    // Filed « Delay » by its maker, a phase tool: believed, the answer corrects it.
    const auto filed = buses::recognise("UAD Little Labs IBP", "Fx|Delay", false, std::nullopt);
    CHECK((filed.kind == Kind::delay));
    CHECK((filed.by == KnownBy::catalogue));
    const auto corrected = buses::recognise("UAD Little Labs IBP", "Fx|Delay", false, Kind::other);
    CHECK((corrected.kind == Kind::other));
    CHECK((corrected.by == KnownBy::answer));
}

TEST_CASE("A sure word of the name, read word by word")
{
    for (const auto* name : {"ValhallaVintageVerb", "UAD RealVerb-Pro", "My Reverb"})
    {
        const auto said = buses::recognise(name, "Fx", false, std::nullopt);
        CHECK_MESSAGE((said.kind == Kind::reverb), name);
        CHECK((said.by == KnownBy::name));
    }
    for (const auto* name : {"ValhallaFreqEcho", "TapeDelay", "echo boy"})
    {
        const auto said = buses::recognise(name, "", false, std::nullopt);
        CHECK_MESSAGE((said.kind == Kind::delay), name);
        CHECK((said.by == KnownBy::name));
    }
    // Both: asked, nothing suggested.
    const auto both = buses::recognise("Reverb Delay", "Fx", false, std::nullopt);
    CHECK_FALSE(both.kind.has_value());
    CHECK_FALSE(both.suggested.has_value());
}

TEST_CASE("Hard to tell: asked, with what the name suggests")
{
    const auto plate = buses::recognise("ValhallaPlate", "Fx", false, std::nullopt);
    CHECK_FALSE(plate.kind.has_value());
    CHECK((plate.by == KnownBy::unknown));
    CHECK((plate.suggested == Kind::reverb));

    // A microphone model: « room » suggests, the person says no.
    CHECK((buses::recognise("TR5 Mic Room", "Fx", false, std::nullopt).suggested == Kind::reverb));
    CHECK((buses::recognise("TR5 Mic Room", "Fx", false, Kind::other).kind == Kind::other));

    CHECK((buses::recognise("MultiTap Thing", "Fx", false, std::nullopt).suggested == Kind::delay));

    // « Valhalla » is one word: it never reads as « hall ».
    const auto massive = buses::recognise("ValhallaSupermassive", "Fx", false, std::nullopt);
    CHECK_FALSE(massive.kind.has_value());
    CHECK_FALSE(massive.suggested.has_value());
    CHECK_FALSE(buses::recognise("ValhallaUberMod", "Fx", false, std::nullopt).suggested.has_value());
}

TEST_CASE("An answer is kept as a word and read back")
{
    for (const auto kind : {Kind::reverb, Kind::delay, Kind::instrument, Kind::other})
        CHECK((buses::kindFromString(buses::toString(kind)) == kind));
    CHECK_FALSE(buses::kindFromString("hall").has_value());
}

namespace
{

PluginInstance named(const std::string& name, const std::string& identifier)
{
    PluginInstance plugin{};
    plugin.id = PluginId::generate();
    plugin.ref = PluginRef{std::string{PluginRef::vst3Format}, identifier, name};
    plugin.params = {{"mix", 0.3}};
    plugin.state.digest = std::string(BlobRef::digestLength, 'a');
    plugin.state.byteCount = 64;
    return plugin;
}

} // namespace

TEST_CASE("A plugin hard to tell on two tracks: a question, answered a proposal, « neither » nothing")
{
    Harness harness;
    for (const auto* name : {"Voix", "Choeur", "Guitare"})
    {
        const auto id = TrackId::generate();
        REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(id, name)).ok());
        REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(id, named("ValhallaPlate", "plate01"), 0))
                    .ok());
    }

    std::map<std::string, Kind> answers;
    const buses::Recognise recognise = [&](const PluginRef& ref)
    {
        const auto answered = answers.find(ref.identifier);
        return buses::recognise(ref.name,
                                "Fx",
                                false,
                                answered == answers.end() ? std::nullopt
                                                          : std::optional<Kind>{answered->second});
    };

    auto proposals = buses::propose(harness.state, recognise);
    REQUIRE(proposals.size() == 1);
    CHECK(proposals.front().toAsk());
    CHECK((proposals.front().suggested == Kind::reverb));
    CHECK(proposals.front().tracks.size() == 3);
    MESSAGE(proposals.front().sentence);
    CHECK(proposals.front().sentence.find("une réverbération, d'après son nom ?") != std::string::npos);

    answers["plate01"] = Kind::reverb;
    proposals = buses::propose(harness.state, recognise);
    REQUIRE(proposals.size() == 1);
    CHECK_FALSE(proposals.front().toAsk());
    CHECK((proposals.front().by == KnownBy::answer));
    CHECK(proposals.front().sentence.find("La même réverbération (ValhallaPlate)") == 0);

    answers["plate01"] = Kind::delay;
    proposals = buses::propose(harness.state, recognise);
    REQUIRE(proposals.size() == 1);
    MESSAGE(proposals.front().sentence);
    CHECK(proposals.front().sentence.find("Le même écho (ValhallaPlate)") == 0);
    CHECK(proposals.front().sentence.find("un bus d'envoi le porte") != std::string::npos);

    answers["plate01"] = Kind::other;
    CHECK(buses::propose(harness.state, recognise).empty());
}
