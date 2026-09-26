#include "TestSupport.h"
#include "daw/domain/commands/AutomationCommands.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/ui/model/AutomationEditing.h"

#include <cmath>

using namespace daw::domain;
using daw::testing::Harness;
namespace automationEditing = daw::ui::automationEditing;

TEST_CASE("the right-click on a slider opens its line, creating it once")
{
    Harness harness;
    const auto target = AutomationTarget::volumeOf(harness.trackId);

    const auto first = automationEditing::open(harness.bus, harness.state, target);
    REQUIRE_FALSE(first.isNil());
    CHECK(harness.bus.undoDepth() == 1);

    // Again: the same line, nothing written.
    CHECK(automationEditing::open(harness.bus, harness.state, target) == first);
    CHECK(harness.bus.undoDepth() == 1);

    // A strip that is not there: refused, nothing written.
    CHECK(automationEditing::open(harness.bus, harness.state, AutomationTarget::panOf(TrackId::generate()))
              .isNil());
    CHECK(harness.bus.undoDepth() == 1);
}

TEST_CASE("lanes come strip by strip (channels, buses, the master), then volume, pan, plugins")
{
    Harness harness;
    const auto busId = TrackId::generate();
    REQUIRE(harness.bus.execute(std::make_unique<AddBus>(busId, "Reverb")).ok());

    PluginInstance synth{};
    synth.id = PluginId::generate();
    synth.ref.format = std::string{PluginRef::vst3Format};
    synth.ref.identifier = "1234567890abcdef1234567890abcdef";
    synth.ref.name = "Vital";
    REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, synth, 0)).ok());

    // Created out of order on purpose.
    for (const auto& target : {AutomationTarget::volumeOf(ProjectState::masterTrackId()),
                               AutomationTarget::parameterOf(synth.id, "12"),
                               AutomationTarget::volumeOf(busId),
                               AutomationTarget::panOf(harness.trackId),
                               AutomationTarget::volumeOf(harness.trackId)})
        REQUIRE_FALSE(automationEditing::open(harness.bus, harness.state, target).isNil());

    std::vector<std::string> labels;
    for (const auto* line : automationEditing::ordered(harness.state))
        labels.push_back(automationEditing::label(harness.state, *line));

    CHECK(labels == std::vector<std::string>{"Piste 1 · Volume",
                                             "Piste 1 · Pan",
                                             "Piste 1 · Vital · 12",
                                             "Reverb · Volume",
                                             "Master · Volume"});
}

TEST_CASE("a lane draws the volume as its fader position, and reads it back")
{
    const auto volume = AutomationTarget::volumeOf(TrackId::generate());
    CHECK(automationEditing::heightOf(volume, ProjectState::maxVolumeDb) == doctest::Approx(1.0));
    CHECK(automationEditing::heightOf(volume, ProjectState::minVolumeDb) == doctest::Approx(0.0));
    CHECK(automationEditing::valueAtHeight(volume, automationEditing::heightOf(volume, -12.0)) ==
          doctest::Approx(-12.0));

    const auto pan = AutomationTarget::panOf(TrackId::generate());
    CHECK(automationEditing::heightOf(pan, 0.0) == doctest::Approx(0.5));
    CHECK(automationEditing::valueAtHeight(pan, 1.0) == doctest::Approx(1.0));
    CHECK(automationEditing::valueAtHeight(pan, 7.0) == doctest::Approx(1.0));
}

TEST_CASE("a wheel notch lands on a round value, and never outside the range")
{
    const auto volume = AutomationTarget::volumeOf(TrackId::generate());
    CHECK(automationEditing::stepValue(volume, -6.4, 1) == doctest::Approx(-5.0));
    CHECK(automationEditing::stepValue(volume, 5.5, 3) == doctest::Approx(6.0));

    const auto pan = AutomationTarget::panOf(TrackId::generate());
    CHECK(automationEditing::stepValue(pan, 0.0, -1) == doctest::Approx(-0.05));
    CHECK(automationEditing::stepValue(pan, -0.98, -1) == doctest::Approx(-1.0));

    CHECK(automationEditing::stepCurve(0.0, 1) == doctest::Approx(0.1));
    CHECK(automationEditing::stepCurve(0.95, 2) == doctest::Approx(1.0));
}
