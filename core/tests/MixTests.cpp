#include "TestSupport.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SampleCommands.h"
#include "daw/domain/serialization/Json.h"

#include <memory>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;

namespace
{

// Two channels, a drum bus and a reverb bus, nothing routed yet.
struct Mixer
{
    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    CommandBus bus{state, registry};
    TrackId kick{TrackId::generate()};
    TrackId hat{TrackId::generate()};
    TrackId drums{TrackId::generate()};
    TrackId reverb{TrackId::generate()};

    Mixer()
    {
        REQUIRE(bus.execute(std::make_unique<AddTrack>(kick, "Kick", 0.0)).ok());
        REQUIRE(bus.execute(std::make_unique<AddTrack>(hat, "Hat", 0.0)).ok());
        REQUIRE(bus.execute(std::make_unique<AddBus>(drums, "Batterie")).ok());
        REQUIRE(bus.execute(std::make_unique<AddBus>(reverb, "Réverbe")).ok());
    }

    [[nodiscard]] std::string written() const { return json::write(state.toValue()); }
};

} // namespace

TEST_CASE("A project without a mix is written the way it was before buses existed")
{
    ProjectState state;
    Track track{};
    track.id = TrackId::generate();
    track.name = "Piste";
    REQUIRE(state.addTrack(track).ok());

    const auto written = json::write(state.toValue());
    CHECK(written.find("buses") == std::string::npos);
    CHECK(written.find("master") == std::string::npos);
    CHECK(written.find("output") == std::string::npos);
    CHECK(written.find("sends") == std::string::npos);
    CHECK(written.find("soloed") == std::string::npos);
}

TEST_CASE("Buses and the master are strips: the channel's own verbs work on them")
{
    Mixer mix;
    const auto master = ProjectState::masterTrackId();

    CHECK(mix.state.tracks().size() == 2);
    CHECK(mix.state.buses().size() == 2);
    CHECK(mix.state.findTrack(mix.drums) == nullptr); // not a channel
    REQUIRE(mix.state.findStrip(mix.drums) != nullptr);

    REQUIRE(mix.bus.execute(std::make_unique<SetTrackVolume>(mix.drums, -4.0)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackPan>(mix.drums, 0.25)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackMuted>(mix.reverb, true)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<RenameTrack>(mix.reverb, "Plate")).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackVolume>(master, -1.5)).ok());

    PluginInstance limiter{};
    limiter.id = PluginId::generate();
    limiter.ref = PluginRef{"VST3", "limiter-id", "Limiter"};
    REQUIRE(mix.bus.execute(std::make_unique<InsertPlugin>(master, limiter, 0)).ok());

    CHECK(mix.state.findStrip(mix.drums)->volumeDb == -4.0);
    CHECK(mix.state.findStrip(mix.reverb)->name == "Plate");
    CHECK(mix.state.master().volumeDb == -1.5);
    CHECK(mix.state.master().plugins.size() == 1);
    CHECK(mix.state.pluginLocation(limiter.id).value().trackId == master);

    const auto written = mix.written();
    CHECK(written.find("\"master\"") != std::string::npos);
}

TEST_CASE("A bus plays nothing: no pattern row, no sample, no audio clip lands on one")
{
    Mixer mix;
    const auto patternId = PatternId::generate();
    REQUIRE(mix.bus.execute(std::make_unique<CreatePattern>(patternId, "", 4.0)).ok());

    CHECK_FALSE(
        mix.bus.execute(std::make_unique<AddPatternTrack>(patternId, ClipId::generate(), mix.drums)).ok());

    SampleRef sample{};
    sample.blob = BlobRef{std::string(64, 'a'), 10};
    sample.name = "Kick.wav";
    sample.format = "wav";
    sample.seconds = 0.5;
    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackSample>(mix.drums, sample)).ok());
    CHECK_FALSE(
        mix.bus.execute(std::make_unique<PlaceAudio>(AudioClipId::generate(), mix.drums, sample, 0.0)).ok());
}

TEST_CASE("Routes go to buses, never in a loop, and an undo gives the old route back")
{
    Mixer mix;

    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.kick, mix.drums)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.drums, mix.reverb)).ok());
    CHECK(mix.state.findStrip(mix.kick)->output == mix.drums);
    CHECK(mix.state.reaches(mix.kick, mix.reverb));

    // Back into its own source: refused, and the refusal leaves nothing.
    const auto before = mix.written();
    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.reverb, mix.drums)).ok());
    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.drums, mix.drums)).ok());
    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.kick, mix.hat)).ok()); // not a bus
    CHECK_FALSE(
        mix.bus.execute(std::make_unique<SetTrackOutput>(ProjectState::masterTrackId(), mix.drums)).ok());
    CHECK(mix.written() == before);

    // The master, by an empty output.
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.kick, TrackId{})).ok());
    CHECK(mix.state.findStrip(mix.kick)->output.isNil());
    REQUIRE(mix.bus.undo().ok());
    CHECK(mix.state.findStrip(mix.kick)->output == mix.drums);
}

TEST_CASE("A send is made by its first touch, one gesture is one entry, and its undo takes the send away")
{
    Mixer mix;

    const auto gesture = mix.bus.beginGesture("envoi");
    for (int step = 0; step <= 10; ++step)
        REQUIRE(mix.bus
                    .execute(std::make_unique<SetTrackSend>(mix.kick, mix.reverb, -30.0 + step),
                             ExecuteOptions{gesture})
                    .ok());
    REQUIRE(mix.bus.endGesture(gesture).ok());

    const auto* kick = mix.state.findStrip(mix.kick);
    REQUIRE(kick->sends.size() == 1);
    CHECK(kick->sends[0].levelDb == -20.0);
    CHECK(mix.bus.undoDepth() == 5); // two tracks, two buses, one send

    REQUIRE(mix.bus.undo().ok());
    CHECK(mix.state.findStrip(mix.kick)->sends.empty());

    // Not to a channel, not to itself, not twice in a loop.
    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.kick, mix.hat, -10.0)).ok());
    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.drums, mix.drums, -10.0)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.reverb, mix.drums)).ok());
    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.drums, mix.reverb, -10.0)).ok());
}

TEST_CASE("Removing a send, then undoing, puts the same send back in the same place")
{
    Mixer mix;
    const auto third = TrackId::generate();
    REQUIRE(mix.bus.execute(std::make_unique<AddBus>(third, "Delay")).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.kick, mix.reverb, -12.0)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.kick, third, -18.0)).ok());

    const auto before = mix.written();
    REQUIRE(mix.bus.execute(std::make_unique<RemoveTrackSend>(mix.kick, mix.reverb)).ok());
    CHECK(mix.state.findStrip(mix.kick)->sends.size() == 1);
    REQUIRE(mix.bus.undo().ok());
    CHECK(mix.written() == before);
}

TEST_CASE("Removing a bus sends what went into it back to the master, and an undo restores it all")
{
    Mixer mix;
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.kick, mix.drums)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.hat, mix.drums)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.hat, mix.reverb, -9.0)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.drums, mix.reverb, -15.0)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackVolume>(mix.reverb, -3.0)).ok());

    const auto before = mix.written();

    REQUIRE(mix.bus.execute(std::make_unique<RemoveTrack>(mix.reverb)).ok());
    CHECK(mix.state.buses().size() == 1);
    CHECK(mix.state.findStrip(mix.hat)->sends.empty());
    CHECK(mix.state.findStrip(mix.drums)->sends.empty());

    REQUIRE(mix.bus.execute(std::make_unique<RemoveTrack>(mix.drums)).ok());
    CHECK(mix.state.findStrip(mix.kick)->output.isNil());
    CHECK(mix.state.findStrip(mix.hat)->output.isNil());

    REQUIRE(mix.bus.undo().ok());
    REQUIRE(mix.bus.undo().ok());
    CHECK(mix.written() == before);

    CHECK_FALSE(mix.bus.execute(std::make_unique<RemoveTrack>(ProjectState::masterTrackId())).ok());
}

TEST_CASE("Solo: one rule for who is heard, and the master never goes in solo")
{
    Mixer mix;
    const auto master = ProjectState::masterTrackId();
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.kick, mix.drums)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.kick, mix.reverb, -12.0)).ok());

    // No solo: mute alone decides.
    CHECK(mix.state.isAudible(mix.kick));
    CHECK(mix.state.isAudible(mix.hat));
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackMuted>(mix.hat, true)).ok());
    CHECK_FALSE(mix.state.isAudible(mix.hat));
    REQUIRE(mix.bus.undo().ok());

    // The kick in solo: its bus and its reverb stay, the hat goes.
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSolo>(mix.kick, true)).ok());
    CHECK(mix.state.isAudible(mix.kick));
    CHECK(mix.state.isAudible(mix.drums));
    CHECK(mix.state.isAudible(mix.reverb));
    CHECK_FALSE(mix.state.isAudible(mix.hat));
    CHECK(mix.state.isAudible(master));
    REQUIRE(mix.bus.undo().ok());

    // The drum bus in solo: what goes into it stays, the rest goes.
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSolo>(mix.drums, true)).ok());
    CHECK(mix.state.isAudible(mix.drums));
    CHECK(mix.state.isAudible(mix.kick));
    CHECK_FALSE(mix.state.isAudible(mix.hat));
    CHECK_FALSE(mix.state.isAudible(mix.reverb));

    // A muted strip in solo is still muted.
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackMuted>(mix.drums, true)).ok());
    CHECK_FALSE(mix.state.isAudible(mix.drums));

    CHECK_FALSE(mix.bus.execute(std::make_unique<SetTrackSolo>(master, true)).ok());
}

TEST_CASE("The mixer survives the round trip, and each new command replays from its payload")
{
    Mixer mix;
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackOutput>(mix.kick, mix.drums)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSend>(mix.hat, mix.reverb, -9.5)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackSolo>(mix.hat, true)).ok());
    REQUIRE(mix.bus.execute(std::make_unique<SetTrackVolume>(ProjectState::masterTrackId(), -2.0)).ok());

    auto reread = ProjectState::fromValue(mix.state.toValue());
    REQUIRE(reread.ok());
    CHECK(reread.value() == mix.state);
    CHECK(json::write(reread.value().toValue()) == mix.written());

    const std::unique_ptr<Command> commands[] = {
        std::make_unique<AddBus>(TrackId::generate(), "Bus"),
        std::make_unique<SetTrackOutput>(mix.kick, TrackId{}),
        std::make_unique<SetTrackSend>(mix.kick, mix.reverb, -6.0),
        std::make_unique<RemoveTrackSend>(mix.hat, mix.reverb),
        std::make_unique<SetTrackSolo>(mix.hat, false),
    };
    for (const auto& command : commands)
    {
        auto rebuilt = mix.registry.create(std::string{command->type()}, command->payload());
        REQUIRE(rebuilt.ok());
        CHECK(rebuilt.value()->payload() == command->payload());
    }
}

TEST_CASE("A project holds 32 buses at most, the number Tracktion gives it")
{
    ProjectState state;
    for (std::size_t index = 0; index < ProjectState::maxBuses; ++index)
    {
        Track bus{};
        bus.id = TrackId::generate();
        bus.name = "Bus";
        REQUIRE(state.insertBus(bus, index).ok());
    }

    Track extra{};
    extra.id = TrackId::generate();
    extra.name = "Un de trop";
    CHECK_FALSE(state.insertBus(extra, 0).ok());
}
