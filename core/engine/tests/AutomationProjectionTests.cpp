#include "EngineTestSupport.h"
#include "daw/domain/commands/AutomationCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/engine/Rendering.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

namespace
{

// The proof of this file is a render cut in windows: an automation is a level
// that changes in time, so a test that read the curve back would prove that a
// number was stored, not that anything was heard.
struct Windows
{
    std::vector<double> left;
    std::vector<double> right;
    double seconds{0.0};
};

Windows renderInWindows(tracktion::Edit& edit, double windowSeconds)
{
    auto file = std::make_unique<juce::TemporaryFile>(".wav");
    REQUIRE(daw::engine::renderAsPlayed(edit, file->getFile()));
    const auto rendered = tracktion::test_utilities::loadBufferAndSampleRate(std::move(file));
    REQUIRE(rendered.buffer.getNumChannels() == 2);

    Windows windows{};
    windows.seconds = rendered.buffer.getNumSamples() / rendered.sampleRate;
    const auto size = static_cast<int>(windowSeconds * rendered.sampleRate);
    for (int start = 0; start + size <= rendered.buffer.getNumSamples(); start += size)
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            double sum = 0.0;
            const auto* samples = rendered.buffer.getReadPointer(channel, start);
            for (int index = 0; index < size; ++index)
                sum += static_cast<double>(samples[index]) * samples[index];
            (channel == 0 ? windows.left : windows.right).push_back(std::sqrt(sum / size));
        }
    }
    return windows;
}

double decibels(double rms)
{
    return rms > 0.0 ? 20.0 * std::log10(rms) : -200.0;
}

// A note on every beat for `beats` beats: a level that holds still unless
// something moves it.
void fill(EngineHarness& harness, int beats)
{
    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, static_cast<double>(beats))).ok());
    for (int beat = 0; beat < beats; ++beat)
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

AutomationPoint point(double beats, double value)
{
    AutomationPoint made{};
    made.id = AutomationPointId::generate();
    made.beats = beats;
    made.value = value;
    return made;
}

std::unique_ptr<Command> write(const AutomationTarget& target, std::vector<AutomationPoint> points)
{
    return std::make_unique<WriteAutomation>(
        AutomationLineId::generate(), target, 0.0, 1000.0, std::move(points));
}

std::string listed(const std::vector<double>& values)
{
    std::string text;
    for (const auto value : values)
        text += juce::String(decibels(value), 1).toStdString() + " ";
    return text;
}

} // namespace

TEST_CASE("A falling volume line is heard falling, window after window")
{
    EngineHarness harness;
    fill(harness, 16); // 8 s at 120 bpm

    const auto flat = renderInWindows(harness.host.edit(), 1.0);
    REQUIRE(flat.left.size() >= 7);

    REQUIRE(harness.bus
                .execute(
                    write(AutomationTarget::volumeOf(harness.trackId), {point(0.0, 0.0), point(16.0, -40.0)}))
                .ok());
    const auto fading = renderInWindows(harness.host.edit(), 1.0);
    MESSAGE("static, dB per second: " << listed(flat.left));
    MESSAGE("fading, dB per second: " << listed(fading.left));

    // Each second quieter than the one before, and the last one far down.
    for (std::size_t index = 1; index < 8 && index < fading.left.size(); ++index)
        CHECK(fading.left[index] < fading.left[index - 1]);
    CHECK(decibels(fading.left.front()) > decibels(flat.left.front()) - 3.0);
    CHECK(decibels(fading.left[7]) < decibels(flat.left[7]) - 20.0);

    // Undone: the level holds still again.
    REQUIRE(harness.bus.undo().ok());
    const auto back = renderInWindows(harness.host.edit(), 1.0);
    CHECK(decibels(back.left[7]) == doctest::Approx(decibels(flat.left[7])).epsilon(0.02));
}

TEST_CASE("A pan line from left to right is heard as two levels that cross")
{
    EngineHarness harness;
    fill(harness, 16);

    REQUIRE(
        harness.bus
            .execute(write(AutomationTarget::panOf(harness.trackId), {point(0.0, -1.0), point(16.0, 1.0)}))
            .ok());
    const auto windows = renderInWindows(harness.host.edit(), 1.0);
    MESSAGE("left:  " << listed(windows.left));
    MESSAGE("right: " << listed(windows.right));
    REQUIRE(windows.left.size() >= 8);

    CHECK(windows.left.front() > windows.right.front() * 4.0);
    CHECK(windows.right[7] > windows.left[7] * 4.0);
    for (std::size_t index = 1; index < 8; ++index)
    {
        CHECK(windows.left[index] < windows.left[index - 1]);
        CHECK(windows.right[index] > windows.right[index - 1]);
    }
}

TEST_CASE("A fade-out of the master is heard on the whole mix")
{
    EngineHarness harness;
    fill(harness, 16);

    // The copilot's request, written by hand: the last two bars go to silence.
    REQUIRE(harness.bus
                .execute(write(AutomationTarget::volumeOf(ProjectState::masterTrackId()),
                               {point(8.0, 0.0), point(16.0, ProjectState::minVolumeDb)}))
                .ok());
    const auto windows = renderInWindows(harness.host.edit(), 1.0);
    MESSAGE("master fade: " << listed(windows.left));
    REQUIRE(windows.left.size() >= 8);

    CHECK(windows.left[3] == doctest::Approx(windows.left[1]).epsilon(0.05));
    for (std::size_t index = 5; index < 8; ++index)
        CHECK(windows.left[index] < windows.left[index - 1]);
    CHECK(decibels(windows.left[7]) < decibels(windows.left[3]) - 12.0);
}

TEST_CASE("Points stay on their beats when the tempo changes")
{
    EngineHarness harness;
    fill(harness, 16);

    // Loud for eight beats, then cut: a step in the level the render can time.
    REQUIRE(harness.bus
                .execute(write(AutomationTarget::volumeOf(harness.trackId),
                               {point(0.0, 0.0), point(8.0, 0.0), point(8.25, ProjectState::minVolumeDb)}))
                .ok());

    const auto cutAt = [&harness]
    {
        const auto windows = renderInWindows(harness.host.edit(), 0.25);
        for (std::size_t index = 0; index < windows.left.size(); ++index)
        {
            if (decibels(windows.left[index]) < -80.0)
                return static_cast<double>(index) * 0.25;
        }
        return -1.0;
    };

    const auto at120 = cutAt();
    REQUIRE(harness.bus.execute(std::make_unique<SetTempoPointBpm>(ProjectState::originTempoPointId(), 60.0))
                .ok());
    const auto at60 = cutAt();
    MESSAGE("silent from " << at120 << " s at 120 bpm, from " << at60 << " s at 60 bpm");

    // Beat 8 is at 4 s, then at 8 s: the cut followed the beat.
    CHECK(at120 == doctest::Approx(4.25).epsilon(0.07));
    CHECK(at60 == doctest::Approx(8.5).epsilon(0.07));
}

TEST_CASE("Pattern mode plays no automation, song mode plays it again")
{
    EngineHarness harness;
    fill(harness, 8);

    REQUIRE(
        harness.bus
            .execute(write(AutomationTarget::volumeOf(harness.trackId),
                           {point(0.0, ProjectState::minVolumeDb), point(8.0, ProjectState::minVolumeDb)}))
            .ok());
    const auto silent = renderInWindows(harness.host.edit(), 1.0);
    CHECK(decibels(silent.left.front()) < -80.0);

    const auto pattern = harness.state.patterns().front().id;
    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::pattern, pattern)).ok());
    const auto auditioned = renderInWindows(harness.host.edit(), 1.0);
    CHECK(decibels(auditioned.left.front()) > -40.0);

    REQUIRE(harness.bus.execute(std::make_unique<TransportSetMode>(PlayMode::song, PatternId{})).ok());
    const auto again = renderInWindows(harness.host.edit(), 1.0);
    CHECK(decibels(again.left.front()) < -80.0);
}

TEST_CASE("Removing the line gives the fader back its own level")
{
    EngineHarness harness;
    fill(harness, 8);
    const auto flat = renderInWindows(harness.host.edit(), 1.0);

    const auto line = AutomationLineId::generate();
    REQUIRE(harness.bus
                .execute(std::make_unique<WriteAutomation>(line,
                                                           AutomationTarget::volumeOf(harness.trackId),
                                                           0.0,
                                                           8.0,
                                                           std::vector<AutomationPoint>{point(0.0, -30.0)}))
                .ok());
    const auto lowered = renderInWindows(harness.host.edit(), 1.0);
    CHECK(decibels(lowered.left[1]) == doctest::Approx(decibels(flat.left[1]) - 30.0).epsilon(0.02));

    REQUIRE(harness.bus.execute(std::make_unique<RemoveAutomationLine>(line)).ok());
    const auto restored = renderInWindows(harness.host.edit(), 1.0);
    CHECK(decibels(restored.left[1]) == doctest::Approx(decibels(flat.left[1])).epsilon(0.02));
}
