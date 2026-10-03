#include "TestSupport.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TransportCommands.h"
#include "daw/ui/history/HistoryLog.h"

#include <memory>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;
using daw::ui::HistoryLog;

namespace
{

// A bus with the log already listening, which is the only way the log is ever
// filled: it is told, it never reads.
struct Logged
{
    Harness harness;
    HistoryLog log;

    Logged() { static_cast<void>(harness.bus.addObserver(log)); }
};

} // namespace

TEST_CASE("an entry per command, in the order they were executed")
{
    Logged fixture;

    REQUIRE(fixture.harness.bus.execute(std::make_unique<AddTrack>(TrackId::generate(), "Basse")).ok());
    REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-6.0)).ok());

    REQUIRE(fixture.log.entries().size() == 2);
    CHECK(fixture.log.entries()[0].type == "track.add");
    CHECK(fixture.log.entries()[1].type == "track.set_volume");
    CHECK(fixture.log.cursor() == 2);
    CHECK(fixture.log.cursor() == fixture.harness.bus.undoDepth());
}

TEST_CASE("a transport command leaves no entry")
{
    Logged fixture;

    REQUIRE(fixture.harness.bus.execute(std::make_unique<TransportPlay>()).ok());

    // It is executed, and observers are told, and it is not in the undo stack.
    // An entry here would be a line in the panel no undo can ever reach.
    CHECK(fixture.log.entries().empty());
    CHECK(fixture.log.cursor() == fixture.harness.bus.undoDepth());
}

TEST_CASE("a gesture is one entry that says how many commands it holds")
{
    Logged fixture;

    const auto gesture = fixture.harness.bus.beginGesture("volume de piste");
    ExecuteOptions options{};
    options.gesture = gesture;

    for (int frame = 1; frame <= 60; ++frame)
    {
        REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-frame * 0.1), options).ok());
    }
    REQUIRE(fixture.harness.bus.endGesture(gesture).ok());

    REQUIRE(fixture.log.entries().size() == 1);
    CHECK(fixture.log.entries()[0].merged == 60);
    CHECK(fixture.log.cursor() == 1);
}

TEST_CASE("undo moves the cursor and keeps the entry, redo puts it back")
{
    Logged fixture;

    REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-3.0)).ok());
    REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-9.0)).ok());

    REQUIRE(fixture.harness.bus.undo().ok());
    CHECK(fixture.log.entries().size() == 2);
    CHECK(fixture.log.cursor() == 1);
    CHECK(fixture.log.cursor() == fixture.harness.bus.undoDepth());

    REQUIRE(fixture.harness.bus.redo().ok());
    CHECK(fixture.log.cursor() == 2);
}

TEST_CASE("executing after an undo drops the redo branch here too")
{
    Logged fixture;

    REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-3.0)).ok());
    REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-9.0)).ok());
    REQUIRE(fixture.harness.bus.undo().ok());

    REQUIRE(fixture.harness.bus.execute(std::make_unique<SetTrackMuted>(fixture.harness.trackId, true)).ok());

    // The entry that could no longer be redone is gone from the list: an
    // interface offering a redo the bus refuses is worse than one offering
    // none.
    REQUIRE(fixture.log.entries().size() == 2);
    CHECK(fixture.log.entries()[1].type == "track.set_muted");
    CHECK(fixture.log.cursor() == 2);
    CHECK(!fixture.harness.bus.canRedo());
}

TEST_CASE("the author of an entry is the actor that executed it")
{
    Logged fixture;

    ExecuteOptions options{};
    options.origin = Provenance{Actor::copilot, std::nullopt};
    REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-4.0), options).ok());

    REQUIRE(fixture.log.entries().size() == 1);
    CHECK(fixture.log.entries()[0].actor == Actor::copilot);

    // A user undoing the copilot's move does not become its author.
    REQUIRE(fixture.harness.bus.undo(Provenance{}).ok());
    CHECK(fixture.log.entries()[0].actor == Actor::copilot);
}

TEST_CASE("the oldest entries go when the bus drops them")
{
    BusLimits limits{};
    limits.maxUndoDepth = 3;

    Harness harness{limits};
    HistoryLog log;
    static_cast<void>(harness.bus.addObserver(log));

    for (int step = 1; step <= 5; ++step)
        REQUIRE(harness.bus.execute(harness.setVolume(-step)).ok());

    CHECK(log.entries().size() == 3);
    CHECK(log.cursor() == harness.bus.undoDepth());
}

TEST_CASE("a command type has a label, and an unknown one keeps its name")
{
    CHECK(HistoryLog::describe("track.add") == "Nouvelle piste");
    CHECK(HistoryLog::describe("note.move") == "Note déplacée");
    CHECK(HistoryLog::describe("mystere.inconnu") == "mystere.inconnu");
}

TEST_CASE("a group is one line, holding the sentence that was asked")
{
    Logged fixture;

    const auto trackId = TrackId::generate();

    PluginInstance vital{};
    vital.id = PluginId::generate();
    vital.ref.format = std::string{PluginRef::clapFormat};
    vital.ref.identifier = "audio.vital.synth";
    vital.ref.name = "Vital";

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<AddTrack>(trackId, "Basse"));
    commands.push_back(std::make_unique<InsertPlugin>(trackId, vital, 0));

    GroupOptions options{};
    options.label = "ajoute une piste Basse et mets-y Vital";
    options.origin.actor = Actor::copilot;

    REQUIRE(fixture.harness.bus.executeGroup(std::move(commands), options).ok());

    // Two commands, two notifications, one line — and the line says what was
    // asked, not what the first command was called.
    REQUIRE(fixture.log.entries().size() == 1);
    CHECK(fixture.log.entries().front().merged == 2);
    CHECK(fixture.log.entries().front().label() == "ajoute une piste Basse et mets-y Vital");
    CHECK(fixture.log.entries().front().actor == Actor::copilot);
    CHECK(fixture.log.cursor() == 1);
    CHECK(fixture.log.cursor() == fixture.harness.bus.undoDepth());

    // And one undo takes the line back, as it takes the whole group back.
    REQUIRE(fixture.harness.bus.undo().ok());
    CHECK(fixture.log.cursor() == 0);
    CHECK(fixture.log.entries().size() == 1);
}

TEST_CASE("a line keeps what the copilot acted upon, and a user's line has nothing")
{
    Logged fixture;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(fixture.harness.setVolume(-6.0));
    GroupOptions options{};
    options.label = "Mixage par l'IA : 1 réglage";
    options.origin.actor = Actor::copilot;
    options.origin.context = BlobRef{std::string(BlobRef::digestLength, 'a'), 42};
    REQUIRE(fixture.harness.bus.executeGroup(std::move(commands), options).ok());
    REQUIRE(fixture.harness.bus.execute(fixture.harness.setVolume(-3.0)).ok());

    REQUIRE(fixture.log.entries().size() == 2);
    REQUIRE(fixture.log.entries()[0].context.has_value());
    CHECK(fixture.log.entries()[0].context->byteCount == 42);
    CHECK_FALSE(fixture.log.entries()[1].context.has_value());
}

TEST_CASE("the transport of a group leaves no line behind")
{
    Logged fixture;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<SetTempoPointBpm>(ProjectState::originTempoPointId(), 140.0));
    commands.push_back(std::make_unique<TransportSetLoop>(true, 0.0, 4.0));

    GroupOptions options{};
    options.label = "passe le tempo a 140 et fais boucler la lecture";
    options.origin.actor = Actor::copilot;

    REQUIRE(fixture.harness.bus.executeGroup(std::move(commands), options).ok());

    REQUIRE(fixture.log.entries().size() == 1);
    CHECK(fixture.log.entries().front().merged == 1);
    CHECK(fixture.log.entries().front().label() == "passe le tempo a 140 et fais boucler la lecture");
}
