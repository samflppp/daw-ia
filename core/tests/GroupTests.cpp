#include "TestSupport.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/TempoCommands.h"
#include "daw/domain/commands/TransportCommands.h"

using namespace daw::domain;
using namespace daw::testing;

namespace
{

// "add a Bass track and put Vital on it": the request of the copilot that
// makes a group necessary, written here as the two commands it becomes.
std::vector<std::unique_ptr<Command>> trackAndPlugin(TrackId trackId, PluginId pluginId)
{
    PluginInstance vital{};
    vital.id = pluginId;
    vital.ref.format = std::string{PluginRef::clapFormat};
    vital.ref.identifier = "audio.vital.synth";
    vital.ref.name = "Vital";

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<AddTrack>(trackId, "Basse"));
    commands.push_back(std::make_unique<InsertPlugin>(trackId, vital, 0));
    return commands;
}

GroupOptions copilotAsked(std::string label)
{
    GroupOptions options{};
    options.label = std::move(label);
    options.origin.actor = Actor::copilot;
    return options;
}

} // namespace

TEST_CASE("a group of two commands is one history entry, and one undo")
{
    Harness harness;
    RecordingObserver observer;
    static_cast<void>(harness.bus.addObserver(observer));

    const auto trackId = TrackId::generate();
    const auto pluginId = PluginId::generate();

    auto receipts = harness.bus.executeGroup(trackAndPlugin(trackId, pluginId),
                                             copilotAsked("ajoute une piste Basse et mets-y Vital"));
    REQUIRE(receipts.ok());

    // Two commands, two notifications, two journal rows — and one entry.
    CHECK(receipts.value().size() == 2);
    CHECK(observer.executed.size() == 2);
    CHECK(harness.bus.undoDepth() == 1);
    CHECK(harness.bus.journal().size() == 2);

    REQUIRE(harness.state.findTrack(trackId) != nullptr);
    REQUIRE(harness.state.findPlugin(pluginId) != nullptr);

    // The two receipts name the same group, and the copilot as author.
    REQUIRE(observer.executed.front().group.has_value());
    CHECK(observer.executed.front().group->id == observer.executed.back().group->id);
    CHECK(observer.executed.front().group->label == "ajoute une piste Basse et mets-y Vital");
    CHECK(observer.executed.front().origin.actor == Actor::copilot);

    REQUIRE(harness.bus.undo().ok());
    CHECK(observer.undone.size() == 1);
    CHECK(harness.bus.undoDepth() == 0);
    CHECK(harness.state.findTrack(trackId) == nullptr);
    CHECK(harness.state.findPlugin(pluginId) == nullptr);

    REQUIRE(harness.bus.redo().ok());
    CHECK(harness.bus.undoDepth() == 1);
    REQUIRE(harness.state.findTrack(trackId) != nullptr);
    REQUIRE(harness.state.findPlugin(pluginId) != nullptr);
}

TEST_CASE("a command that fails in the middle of a group leaves nothing behind")
{
    Harness harness;
    RecordingObserver observer;
    static_cast<void>(harness.bus.addObserver(observer));

    const auto before = harness.state.toValue();
    const auto trackId = TrackId::generate();

    PluginInstance vital{};
    vital.id = PluginId::generate();
    vital.ref.format = std::string{PluginRef::clapFormat};
    vital.ref.identifier = "audio.vital.synth";
    vital.ref.name = "Vital";

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<AddTrack>(trackId, "Basse"));
    // The second command names a track that does not exist: the domain refuses
    // it exactly as it would refuse it alone.
    commands.push_back(std::make_unique<InsertPlugin>(TrackId::generate(), vital, 0));

    const auto receipts = harness.bus.executeGroup(std::move(commands), copilotAsked("une requête"));
    REQUIRE(!receipts.ok());
    CHECK(receipts.error().code == ErrorCode::notFound);

    // The track the first command added is gone again, no notification was
    // sent, and the journal holds nothing.
    CHECK(harness.state.toValue() == before);
    CHECK(harness.state.findTrack(trackId) == nullptr);
    CHECK(observer.executed.empty());
    CHECK(harness.bus.undoDepth() == 0);
    CHECK(harness.bus.journal().empty());
}

TEST_CASE("a group mixing an edit and a transport leaves one entry and no transport row")
{
    Harness harness;
    RecordingObserver observer;
    static_cast<void>(harness.bus.addObserver(observer));

    const auto clipId = ClipId::generate();
    REQUIRE(harness.bus.execute(harness.createClip(clipId, 0.0, 4.0)).ok());

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<SetTempoPointBpm>(ProjectState::originTempoPointId(), 140.0));
    commands.push_back(std::make_unique<TransportSetLoop>(true, 0.0, 4.0));

    auto receipts = harness.bus.executeGroup(std::move(commands),
                                             copilotAsked("passe le tempo à 140 et fais boucler la lecture"));
    REQUIRE(receipts.ok());
    CHECK(receipts.value().size() == 2);

    CHECK(harness.state.tempoAt(0.0) == doctest::Approx(140.0));
    CHECK(harness.state.transport().looping);

    // The clip was one entry, the group is the second: the transport command
    // added none.
    CHECK(harness.bus.undoDepth() == 2);

    // And the journal holds the clip and the tempo, never the loop.
    const auto journal = harness.bus.journal();
    REQUIRE(journal.size() == 2);
    auto tempoEnvelope = CommandEnvelope::fromValue(journal.back());
    REQUIRE(tempoEnvelope.ok());
    CHECK(tempoEnvelope.value().type == "tempo.set_bpm");
    REQUIRE(tempoEnvelope.value().group.has_value());

    // One Ctrl+Z gives the tempo back and leaves the loop where it is: the
    // transport is not something a history owns.
    REQUIRE(harness.bus.undo().ok());
    CHECK(harness.state.tempoAt(0.0) == doctest::Approx(120.0));
    CHECK(harness.state.transport().looping);
}

TEST_CASE("a group of nothing but transport commands leaves no entry and no redo loss")
{
    Harness harness;

    REQUIRE(harness.bus.execute(harness.setVolume(-6.0)).ok());
    REQUIRE(harness.bus.undo().ok());
    REQUIRE(harness.bus.redoDepth() == 1);

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<TransportSetPosition>(2.0));
    commands.push_back(std::make_unique<TransportPlay>());

    REQUIRE(harness.bus.executeGroup(std::move(commands), copilotAsked("joue à partir de 2")).ok());

    CHECK(harness.bus.undoDepth() == 0);
    CHECK(harness.bus.redoDepth() == 1);
    CHECK(harness.state.transport().playing);
}

TEST_CASE("a replayed group is one entry again, not three")
{
    Harness source;
    const auto trackId = TrackId::generate();
    const auto pluginId = PluginId::generate();

    REQUIRE(source.bus
                .executeGroup(trackAndPlugin(trackId, pluginId),
                              copilotAsked("ajoute une piste Basse et mets-y Vital"))
                .ok());

    // A lone command after the group, on the track the group created: the
    // replay has to rebuild two entries out of three rows.
    REQUIRE(source.bus.execute(std::make_unique<SetTrackVolume>(trackId, -3.0)).ok());

    const auto journal = source.bus.journal();
    REQUIRE(journal.size() == 3);

    // The replay reads the group out of the envelopes and rebuilds the entry.
    Harness replayed;
    std::vector<Value> group{journal[0], journal[1]};
    REQUIRE(replayed.bus.executeSerializedGroup(group).ok());
    REQUIRE(replayed.bus.executeSerialized(journal[2]).ok());

    CHECK(replayed.bus.undoDepth() == 2);
    CHECK(replayed.bus.journal() == journal);

    // And undoing once gives back the whole group, as it did before the
    // project was closed.
    REQUIRE(replayed.bus.undo().ok());
    REQUIRE(replayed.bus.undo().ok());
    CHECK(replayed.state.findTrack(trackId) == nullptr);
    CHECK(replayed.state.findPlugin(pluginId) == nullptr);
}

TEST_CASE("envelopes naming two different groups are refused")
{
    Harness source;
    REQUIRE(source.bus
                .executeGroup(trackAndPlugin(TrackId::generate(), PluginId::generate()),
                              copilotAsked("une requête"))
                .ok());
    REQUIRE(source.bus
                .executeGroup(trackAndPlugin(TrackId::generate(), PluginId::generate()),
                              copilotAsked("une autre"))
                .ok());

    const auto journal = source.bus.journal();
    REQUIRE(journal.size() == 4);

    Harness replayed;
    const std::vector<Value> mixed{journal[0], journal[2]};
    const auto replayedGroup = replayed.bus.executeSerializedGroup(mixed);
    REQUIRE(!replayedGroup.ok());
    CHECK(replayedGroup.error().code == ErrorCode::invalidPayload);
    CHECK(replayed.bus.undoDepth() == 0);
}

TEST_CASE("a group refuses an empty list and an empty label")
{
    Harness harness;

    CHECK(harness.bus.executeGroup({}, copilotAsked("une requête")).error().code ==
          ErrorCode::invalidArgument);

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(harness.setVolume(-6.0));
    CHECK(harness.bus.executeGroup(std::move(commands), copilotAsked("")).error().code ==
          ErrorCode::invalidArgument);

    CHECK(harness.bus.undoDepth() == 0);
}

TEST_CASE("a gesture never merges into a grouped entry")
{
    Harness harness;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(harness.setVolume(-6.0));
    REQUIRE(harness.bus.executeGroup(std::move(commands), copilotAsked("baisse la piste")).ok());
    REQUIRE(harness.bus.undoDepth() == 1);

    // Same command type, same track, and a gesture asking to merge: the entry
    // on top is a group, so it absorbs nothing and a second entry appears.
    const auto gesture = harness.bus.beginGesture("fader");
    ExecuteOptions options{};
    options.gesture = gesture;
    REQUIRE(harness.bus.execute(harness.setVolume(-7.0), options).ok());

    CHECK(harness.bus.undoDepth() == 2);
}
