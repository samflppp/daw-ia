#include "EngineTestSupport.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/engine/LevelMeters.h"
#include "daw/engine/MeterTap.h"
#include "daw/engine/Rendering.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::engine::LevelMeters;
using daw::engine::MeterTapPlugin;
using daw::engine::StripLevel;
using daw::testing::EngineHarness;

namespace
{

// Every figure below is measured on a render, by the taps and by the file:
// what a fader, a route, a send or a solo is asserted to do is what comes out
// of the Edit, never what a field holds.
struct Rendered
{
    std::vector<StripLevel> strips;
    float filePeakDb{StripLevel::floorDb};

    [[nodiscard]] float peakOf(const std::string& strip) const
    {
        const auto found = std::find_if(
            strips.begin(), strips.end(), [&strip](const StripLevel& level) { return level.strip == strip; });
        return found != strips.end() ? found->peakDb : StripLevel::floorDb;
    }

    [[nodiscard]] float master() const { return peakOf(MeterTapPlugin::masterStrip.toStdString()); }
};

Rendered render(EngineHarness& harness)
{
    LevelMeters meters{harness.host.edit()};
    meters.resetTotals();

    auto file = std::make_unique<juce::TemporaryFile>(".wav");
    REQUIRE(daw::engine::renderAsPlayed(harness.host.edit(), file->getFile()));
    const auto buffer = tracktion::test_utilities::loadBufferAndSampleRate(std::move(file));

    Rendered rendered;
    rendered.strips = meters.totals();
    const auto peak = buffer.buffer.getMagnitude(0, buffer.buffer.getNumSamples());
    rendered.filePeakDb =
        peak > 0.0f ? std::max(StripLevel::floorDb, 20.0f * std::log10(peak)) : StripLevel::floorDb;
    return rendered;
}

void fill(EngineHarness& harness, TrackId trackId, int pitch)
{
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<CreateMidiClip>(trackId, clipId, 0.0, 4.0)).ok());
    for (int beat = 0; beat < 4; ++beat)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = pitch;
        note.velocity = 100;
        note.startBeats = static_cast<double>(beat);
        note.lengthBeats = 1.0;
        REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clipId, note)).ok());
    }
}

constexpr float toleranceDb = 0.1f;

} // namespace

TEST_CASE("A track routed into a bus goes through the bus's fader, and the bus's tap measures it")
{
    EngineHarness harness;
    fill(harness, harness.trackId, 60);
    const auto direct = render(harness);
    REQUIRE(direct.master() > -60.0f);

    const auto drums = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddBus>(drums, "Batterie")).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackOutput>(harness.trackId, drums)).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackVolume>(drums, -6.0)).ok());

    const auto routed = render(harness);
    MESSAGE("direct: master " << direct.master() << " dB; through the bus at -6 dB: bus "
                              << routed.peakOf(drums.toString()) << " dB, master " << routed.master()
                              << " dB, file " << routed.filePeakDb << " dB");

    CHECK(std::abs(routed.peakOf(harness.trackId.toString()) - direct.master()) < toleranceDb);
    CHECK(std::abs(routed.peakOf(drums.toString()) - (direct.master() - 6.0f)) < toleranceDb);
    CHECK(std::abs(routed.master() - (direct.master() - 6.0f)) < toleranceDb);
    CHECK(std::abs(routed.filePeakDb - (direct.master() - 6.0f)) < toleranceDb);

    // Its AuxReturn leads the bus's chain, its tap closes it.
    auto* track = [&]() -> tracktion::AudioTrack*
    {
        for (auto* candidate : tracktion::getAudioTracks(harness.host.edit()))
            if (candidate->state.getProperty("dawDomainTrackId").toString() == juce::String(drums.toString()))
                return candidate;
        return nullptr;
    }();
    REQUIRE(track != nullptr);
    CHECK(dynamic_cast<tracktion::AuxReturnPlugin*>(track->pluginList.getPlugins().getFirst().get()) !=
          nullptr);
    CHECK(dynamic_cast<MeterTapPlugin*>(track->pluginList.getPlugins().getLast().get()) != nullptr);

    // Removed, the bus lets the track go back to the master, at full level.
    REQUIRE(harness.bus.execute(std::make_unique<RemoveTrack>(drums)).ok());
    const auto back = render(harness);
    CHECK(std::abs(back.master() - direct.master()) < toleranceDb);
}

TEST_CASE("A send feeds its bus after the channel's fader, and a muted channel sends nothing")
{
    EngineHarness harness;
    fill(harness, harness.trackId, 60);
    const auto direct = render(harness);

    const auto reverb = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddBus>(reverb, "Réverbe")).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSend>(harness.trackId, reverb, -6.0)).ok());
    REQUIRE(harness.bus.execute(harness.setVolume(-3.0)).ok());

    const auto sent = render(harness);
    MESSAGE("channel at -3 dB, send at -6 dB: channel " << sent.peakOf(harness.trackId.toString())
                                                        << " dB, bus " << sent.peakOf(reverb.toString())
                                                        << " dB");

    CHECK(std::abs(sent.peakOf(harness.trackId.toString()) - (direct.master() - 3.0f)) < toleranceDb);
    CHECK(std::abs(sent.peakOf(reverb.toString()) - (direct.master() - 9.0f)) < toleranceDb);

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackMuted>(harness.trackId, true)).ok());
    const auto muted = render(harness);
    CHECK(muted.peakOf(reverb.toString()) < -90.0f);
    CHECK(muted.filePeakDb < -90.0f);

    // An undo of the send's first touch takes the send away: the bus is silent
    // even with the channel back.
    REQUIRE(harness.bus.undo().ok()); // mute
    REQUIRE(harness.bus.undo().ok()); // volume
    REQUIRE(harness.bus.undo().ok()); // send
    const auto unsent = render(harness);
    CHECK(unsent.peakOf(reverb.toString()) < -90.0f);
    CHECK(std::abs(unsent.master() - direct.master()) < toleranceDb);
}

TEST_CASE("Solo leaves only what is in solo, and what it goes through")
{
    EngineHarness harness;
    fill(harness, harness.trackId, 48);

    const auto hat = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(hat, "Hat", 0.0)).ok());
    fill(harness, hat, 84);

    const auto drums = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddBus>(drums, "Batterie")).ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackOutput>(harness.trackId, drums)).ok());

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackMuted>(hat, true)).ok());
    const auto kickAlone = render(harness);
    REQUIRE(harness.bus.undo().ok());

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSolo>(harness.trackId, true)).ok());
    const auto soloed = render(harness);
    MESSAGE("kick in solo: kick " << soloed.peakOf(harness.trackId.toString()) << " dB, bus "
                                  << soloed.peakOf(drums.toString()) << " dB, hat "
                                  << soloed.peakOf(hat.toString()) << " dB, master " << soloed.master()
                                  << " dB (kick alone: " << kickAlone.master() << ")");

    CHECK(soloed.peakOf(hat.toString()) < -90.0f);
    CHECK(soloed.peakOf(drums.toString()) > -60.0f);
    CHECK(std::abs(soloed.master() - kickAlone.master()) < toleranceDb);

    // The solo on the bus: the kick goes through it, the hat is silent.
    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackSolo>(drums, true)).ok());
    const auto busSolo = render(harness);
    CHECK(busSolo.peakOf(hat.toString()) < -90.0f);
    CHECK(std::abs(busSolo.master() - kickAlone.master()) < toleranceDb);
}

TEST_CASE("The master's fader and mute are heard in the file, and its tap reads after them")
{
    EngineHarness harness;
    fill(harness, harness.trackId, 60);
    const auto unity = render(harness);

    const auto master = ProjectState::masterTrackId();
    REQUIRE(harness.bus.execute(std::make_unique<SetTrackVolume>(master, -6.0)).ok());
    const auto lowered = render(harness);
    MESSAGE("master at -6 dB: tap " << lowered.master() << " dB, file " << lowered.filePeakDb << " dB (unity "
                                    << unity.filePeakDb << ")");
    CHECK(std::abs(lowered.master() - (unity.master() - 6.0f)) < toleranceDb);
    CHECK(std::abs(lowered.filePeakDb - (unity.filePeakDb - 6.0f)) < toleranceDb);
    CHECK(std::abs(lowered.peakOf(harness.trackId.toString()) - unity.master()) < toleranceDb);

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackMuted>(master, true)).ok());
    const auto muted = render(harness);
    CHECK(muted.filePeakDb < -90.0f);
    CHECK(muted.master() < -90.0f);
}
