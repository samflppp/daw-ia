#include "EngineTestSupport.h"
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

// Peak and RMS of a render, per channel, computed from the file and from
// nothing else. The meters are judged against this: a test that read back the
// number a meter stored would agree with any meter at all.
struct Measured
{
    double seconds{0.0};
    float peakLeftDb{StripLevel::floorDb};
    float peakRightDb{StripLevel::floorDb};
    float rmsLeftDb{StripLevel::floorDb};
    float rmsRightDb{StripLevel::floorDb};
};

float toDb(double gain)
{
    return gain <= 0.0 ? StripLevel::floorDb
                       : std::max(StripLevel::floorDb, static_cast<float>(20.0 * std::log10(gain)));
}

tracktion::test_utilities::BufferAndSampleRate renderAsPlayed(tracktion::Edit& edit)
{
    auto file = std::make_unique<juce::TemporaryFile>(".wav");
    REQUIRE(daw::engine::renderAsPlayed(edit, file->getFile()));
    return tracktion::test_utilities::loadBufferAndSampleRate(std::move(file));
}

// Renders the Edit with the taps' totals reset just before, so that what they
// report covers the stretch the file does.
Measured renderAndMeasure(EngineHarness& harness, LevelMeters& meters)
{
    meters.resetTotals();
    const auto rendered = renderAsPlayed(harness.host.edit());
    REQUIRE(rendered.buffer.getNumChannels() == 2);
    REQUIRE(rendered.buffer.getNumSamples() > 0);

    Measured measured{};
    double peaks[2] = {0.0, 0.0};
    double sums[2] = {0.0, 0.0};
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* samples = rendered.buffer.getReadPointer(channel);
        for (int index = 0; index < rendered.buffer.getNumSamples(); ++index)
        {
            peaks[channel] = std::max(peaks[channel], static_cast<double>(std::abs(samples[index])));
            sums[channel] += static_cast<double>(samples[index]) * samples[index];
        }
    }

    const auto count = static_cast<double>(rendered.buffer.getNumSamples());
    measured.seconds = count / rendered.sampleRate;
    measured.peakLeftDb = toDb(peaks[0]);
    measured.peakRightDb = toDb(peaks[1]);
    measured.rmsLeftDb = toDb(std::sqrt(sums[0] / count));
    measured.rmsRightDb = toDb(std::sqrt(sums[1] / count));
    return measured;
}

const StripLevel& stripOf(const std::vector<StripLevel>& levels, const std::string& strip)
{
    const auto found = std::find_if(
        levels.begin(), levels.end(), [&strip](const StripLevel& level) { return level.strip == strip; });
    REQUIRE(found != levels.end());
    return *found;
}

void fillFourBeats(EngineHarness& harness)
{
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 4.0)).ok());

    for (int beat = 0; beat < 4; ++beat)
    {
        Note note{};
        note.id = NoteId::generate();
        note.pitch = 60;
        note.velocity = 100;
        note.startBeats = static_cast<double>(beat);
        note.lengthBeats = 1.0;
        REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clipId, note)).ok());
    }
}

// Within a tenth of a decibel: the meter reads blocks as the render plays
// them, the file is read after the fact, and they must agree.
constexpr float toleranceDb = 0.1f;

// An RMS is an energy divided by a duration. The render processes a little
// more than it writes — the blocks past the end of the Edit, all silent — so
// two RMS over two durations are compared as energies: the same sound over a
// longer silence has the same energy and a lower RMS.
float energyDb(float rmsDb, double seconds)
{
    return rmsDb + static_cast<float>(10.0 * std::log10(seconds));
}

} // namespace

TEST_CASE("Every chain ends with one level tap, and the master has one")
{
    EngineHarness harness;
    fillFourBeats(harness);

    // A second, a third projection place nothing more.
    harness.projector.reconcile();
    harness.projector.reconcile();

    auto* track = harness.firstAudioTrack();
    REQUIRE(track != nullptr);

    const auto taps = track->pluginList.getPluginsOfType<MeterTapPlugin>();
    REQUIRE(taps.size() == 1);
    CHECK(track->pluginList.getPlugins().getLast() == taps.getFirst());
    CHECK(taps.getFirst()->strip() == juce::String(harness.trackId.toString()));

    const auto master = harness.host.edit().getMasterPluginList().getPluginsOfType<MeterTapPlugin>();
    REQUIRE(master.size() == 1);
    CHECK(master.getFirst()->strip() == MeterTapPlugin::masterStrip);
}

TEST_CASE("A track meter and the master meter read what the render holds")
{
    EngineHarness harness;
    fillFourBeats(harness);
    LevelMeters meters{harness.host.edit()};

    const auto file = renderAndMeasure(harness, meters);
    const auto totals = meters.totals();
    const auto& track = stripOf(totals, harness.trackId.toString());
    const auto& master = stripOf(totals, MeterTapPlugin::masterStrip.toStdString());

    MESSAGE("file:   peak " << file.peakLeftDb << " / " << file.peakRightDb << " dB, RMS " << file.rmsLeftDb
                            << " / " << file.rmsRightDb << " dB");
    MESSAGE("track:  peak " << track.peakLeftDb << " / " << track.peakRightDb << " dB, RMS "
                            << track.rmsLeftDb << " / " << track.rmsRightDb << " dB");
    MESSAGE("master: peak " << master.peakLeftDb << " / " << master.peakRightDb << " dB, RMS "
                            << master.rmsLeftDb << " / " << master.rmsRightDb << " dB");

    REQUIRE(file.peakLeftDb > -60.0f);

    // One track, a master at unity: the track, the master and the file are
    // three measurements of the same signal.
    for (const auto* strip : {&track, &master})
    {
        CHECK(std::abs(strip->peakLeftDb - file.peakLeftDb) < toleranceDb);
        CHECK(std::abs(strip->peakRightDb - file.peakRightDb) < toleranceDb);
        MESSAGE("measured over " << strip->seconds << " s, file " << file.seconds << " s");
        CHECK(strip->seconds >= file.seconds);
        CHECK(std::abs(energyDb(strip->rmsLeftDb, strip->seconds) - energyDb(file.rmsLeftDb, file.seconds)) <
              toleranceDb);
        CHECK(std::abs(energyDb(strip->rmsRightDb, strip->seconds) -
                       energyDb(file.rmsRightDb, file.seconds)) < toleranceDb);
        CHECK_FALSE(strip->over);
    }
}

TEST_CASE("Lowering a fader by 6 dB lowers its meter by 6 dB, as the file does")
{
    EngineHarness harness;
    fillFourBeats(harness);
    LevelMeters meters{harness.host.edit()};

    const auto fileBefore = renderAndMeasure(harness, meters);
    const auto before = stripOf(meters.totals(), harness.trackId.toString());

    REQUIRE(harness.bus.execute(harness.setVolume(-6.0)).ok());

    const auto fileAfter = renderAndMeasure(harness, meters);
    const auto after = stripOf(meters.totals(), harness.trackId.toString());

    MESSAGE("meter peak " << before.peakDb << " -> " << after.peakDb << " dB, RMS " << before.rmsDb << " -> "
                          << after.rmsDb << " dB; file peak " << fileBefore.peakLeftDb << " -> "
                          << fileAfter.peakLeftDb << " dB");

    CHECK(std::abs((after.peakDb - before.peakDb) + 6.0f) < toleranceDb);
    CHECK(std::abs((after.rmsDb - before.rmsDb) + 6.0f) < toleranceDb);
    CHECK(std::abs((fileAfter.peakLeftDb - fileBefore.peakLeftDb) + 6.0f) < toleranceDb);
    CHECK(std::abs(after.peakLeftDb - fileAfter.peakLeftDb) < toleranceDb);
}

TEST_CASE("A muted track reads silence, and a hard pan empties one side")
{
    EngineHarness harness;
    fillFourBeats(harness);
    LevelMeters meters{harness.host.edit()};

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackPan>(harness.trackId, -1.0)).ok());
    static_cast<void>(renderAndMeasure(harness, meters));
    const auto panned = stripOf(meters.totals(), harness.trackId.toString());
    MESSAGE("pan -1: peak L " << panned.peakLeftDb << " dB, R " << panned.peakRightDb << " dB");

    CHECK(panned.peakLeftDb > -60.0f);
    CHECK(panned.peakRightDb < panned.peakLeftDb - 40.0f);

    REQUIRE(harness.bus.execute(std::make_unique<SetTrackMuted>(harness.trackId, true)).ok());
    const auto file = renderAndMeasure(harness, meters);
    const auto totals = meters.totals();
    const auto& muted = stripOf(totals, harness.trackId.toString());
    const auto& master = stripOf(totals, MeterTapPlugin::masterStrip.toStdString());
    MESSAGE("muted: track " << muted.peakDb << " dB, master " << master.peakDb << " dB, file "
                            << file.peakLeftDb << " dB");

    CHECK(muted.peakDb < -90.0f);
    CHECK(master.peakDb < -90.0f);
    CHECK(file.peakLeftDb < -90.0f);
}

TEST_CASE("A signal past full scale is reported as an over")
{
    EngineHarness harness;
    fillFourBeats(harness);
    LevelMeters meters{harness.host.edit()};

    // Four tracks at +6 dB, each playing a cluster of twelve notes at once:
    // far past 0 dBFS on the master whatever the phases of the voices. One
    // note per track was not enough — 4OSC starts its oscillators at random
    // phases, and four copies of one note can cancel as much as they add.
    std::vector<TrackId> tracks{harness.trackId};
    for (int copy = 0; copy < 3; ++copy)
    {
        tracks.push_back(TrackId::generate());
        REQUIRE(harness.bus.execute(std::make_unique<AddTrack>(tracks.back(), "copie", 6.0)).ok());
    }
    REQUIRE(harness.bus.execute(harness.setVolume(6.0)).ok());

    for (const auto& trackId : tracks)
    {
        const auto clipId = ClipId::generate();
        REQUIRE(harness.bus.execute(std::make_unique<CreateMidiClip>(trackId, clipId, 8.0, 4.0)).ok());
        for (int pitch = 48; pitch < 60; ++pitch)
        {
            Note note{};
            note.id = NoteId::generate();
            note.pitch = pitch;
            note.velocity = 127;
            note.startBeats = 0.0;
            note.lengthBeats = 4.0;
            REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clipId, note)).ok());
        }
    }

    static_cast<void>(renderAndMeasure(harness, meters));
    const auto totals = meters.totals();
    const auto& master = stripOf(totals, MeterTapPlugin::masterStrip.toStdString());
    MESSAGE("master peak " << master.peakDb << " dB");

    CHECK(master.peakDb > 0.0f);
    CHECK(master.over);
}
