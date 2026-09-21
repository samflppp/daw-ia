#include "EngineTestSupport.h"
#include "daw/domain/commands/TrackCommands.h"

#include <tracktion_engine/utilities/tracktion_TestUtilities.h>

#include <cmath>
#include <memory>
#include <utility>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::EngineHarness;

namespace
{

// Root mean square of each channel, separately.
//
// The whole point of this file. Reading Track::pan back, or asking the volume
// plugin what its pan is, would prove that a number was stored where it was
// put. A pan is a position in the stereo field: what it changes is how loud
// the left is against the right, and that is what gets measured.
std::pair<double, double> renderedRmsPerChannel(tracktion::Edit& edit)
{
    const auto rendered = tracktion::test_utilities::renderToAudioBuffer(edit);

    // A mono buffer would make every assertion below meaningless, and it would
    // also be a real bug: VolumeAndPanPlugin applies its right gain only when
    // the buffer has two channels, so a mono track would be attenuated instead
    // of moved. Nothing produces one today — every source is a MIDI instrument
    // — and the day an audio clip does, this line says so.
    REQUIRE(rendered.buffer.getNumChannels() == 2);
    REQUIRE(rendered.buffer.getNumSamples() > 0);

    double sums[2] = {0.0, 0.0};
    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* samples = rendered.buffer.getReadPointer(channel);
        for (int sample = 0; sample < rendered.buffer.getNumSamples(); ++sample)
            sums[channel] += static_cast<double>(samples[sample]) * samples[sample];
    }

    const auto count = static_cast<double>(rendered.buffer.getNumSamples());
    return {std::sqrt(sums[0] / count), std::sqrt(sums[1] / count)};
}

// The law the projection claims to use, recomputed here from its definition
// rather than called from Tracktion: a test that asked Tracktion what Tracktion
// does would agree with any law at all.
//
// PanLaw3dBCenter, constant power: the two gains are sine and cosine of the
// same angle, so their squares add up to one wherever the track sits.
std::pair<double, double> expectedGains(double pan)
{
    const auto position = (pan + 1.0) * 0.5; // -1..+1 becomes 0..1
    const auto quarterTurn = 3.14159265358979323846 * 0.5;
    return {std::sin((1.0 - position) * quarterTurn), std::sin(position * quarterTurn)};
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

std::unique_ptr<Command> setPan(const EngineHarness& harness, double pan)
{
    return std::make_unique<SetTrackPan>(harness.trackId, pan);
}

} // namespace

TEST_CASE("A centred track comes out of both channels alike")
{
    EngineHarness harness;
    fillFourBeats(harness);

    const auto [left, right] = renderedRmsPerChannel(harness.host.edit());
    MESSAGE("pan 0: L = " << left << ", R = " << right);

    REQUIRE(left > 0.001);
    CHECK(right == doctest::Approx(left).epsilon(0.02));
}

TEST_CASE("A track panned hard leaves one channel silent")
{
    EngineHarness harness;
    fillFourBeats(harness);

    const auto [centreLeft, centreRight] = renderedRmsPerChannel(harness.host.edit());
    static_cast<void>(centreRight);

    REQUIRE(harness.bus.execute(setPan(harness, -1.0)).ok());
    const auto [hardLeft, hardRight] = renderedRmsPerChannel(harness.host.edit());
    MESSAGE("pan -1: L = " << hardLeft << ", R = " << hardRight);

    CHECK(hardLeft > 0.001);
    CHECK(hardRight < hardLeft / 100.0);

    REQUIRE(harness.bus.execute(setPan(harness, 1.0)).ok());
    const auto [farLeft, farRight] = renderedRmsPerChannel(harness.host.edit());
    MESSAGE("pan +1: L = " << farLeft << ", R = " << farRight);

    CHECK(farRight > 0.001);
    CHECK(farLeft < farRight / 100.0);

    // Constant power, and this is the audible half of the choice: the centre
    // is 3 dB down on either extreme, so crossing the field does not change
    // how loud the track is. Tracktion's own default law would have made the
    // extreme 6 dB louder than the centre instead.
    CHECK(centreLeft == doctest::Approx(hardLeft * std::sin(3.14159265358979323846 / 4.0)).epsilon(0.03));
}

TEST_CASE("A track half way across follows the law the projection names")
{
    EngineHarness harness;
    fillFourBeats(harness);

    constexpr double pan = 0.5;
    REQUIRE(harness.bus.execute(setPan(harness, pan)).ok());

    const auto [left, right] = renderedRmsPerChannel(harness.host.edit());
    const auto [gainLeft, gainRight] = expectedGains(pan);

    MESSAGE("pan " << pan << ": measured L/R = " << (left / right) << ", law says "
                   << (gainLeft / gainRight));

    CHECK(left / right == doctest::Approx(gainLeft / gainRight).epsilon(0.03));
}

TEST_CASE("Undoing a pan brings the track back to where it was heard")
{
    EngineHarness harness;
    fillFourBeats(harness);

    const auto [centreLeft, centreRight] = renderedRmsPerChannel(harness.host.edit());

    const auto gesture = harness.bus.beginGesture("panoramique");
    for (int frame = 1; frame <= 10; ++frame)
        REQUIRE(
            harness.bus.execute(setPan(harness, static_cast<double>(frame) / 10.0), ExecuteOptions{gesture})
                .ok());
    REQUIRE(harness.bus.endGesture(gesture).ok());

    const auto [pannedLeft, pannedRight] = renderedRmsPerChannel(harness.host.edit());
    CHECK(pannedRight > pannedLeft);

    // One drag, one entry, one undo: the whole sweep is a single gesture.
    REQUIRE(harness.bus.undo().ok());
    const auto [backLeft, backRight] = renderedRmsPerChannel(harness.host.edit());
    MESSAGE("after undo: L = " << backLeft << ", R = " << backRight);

    CHECK(backLeft == doctest::Approx(centreLeft).epsilon(0.02));
    CHECK(backRight == doctest::Approx(centreRight).epsilon(0.02));
}
