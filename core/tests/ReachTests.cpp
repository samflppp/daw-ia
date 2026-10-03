#include "TestSupport.h"
#include "daw/domain/commands/MixCommands.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/NoteEditCommands.h"
#include "daw/domain/commands/PluginCommands.h"
#include "daw/domain/commands/SetTrackVolume.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/InternalEffects.h"
#include "daw/domain/serialization/Json.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::testing::Harness;

namespace
{

// The project as a value, without the notes of any row: what a command that
// reaches notes alone must leave exactly as it was.
Value withoutNotes(const Value& value)
{
    if (const auto* object = value.asObject(); object != nullptr)
    {
        Value::Object kept;
        for (const auto& [key, member] : *object)
            if (key != "notes")
                kept.emplace_back(key, withoutNotes(member));
        return Value::object(std::move(kept));
    }
    if (const auto* array = value.asArray(); array != nullptr)
    {
        Value::Array kept;
        for (const auto& item : *array)
            kept.push_back(withoutNotes(item));
        return Value::array(std::move(kept));
    }
    return value;
}

// A row with two notes, the second off the grid, and something of each kind
// around it.
struct Row
{
    Row()
    {
        REQUIRE(harness.bus.execute(harness.createClip(clip, 0.0, 4.0)).ok());
        REQUIRE(harness.bus.execute(Harness::addNote(clip, first, 60)).ok());
        Note late{};
        late.id = second;
        late.pitch = 64;
        late.velocity = 90;
        late.startBeats = 1.1;
        late.lengthBeats = 0.5;
        REQUIRE(harness.bus.execute(std::make_unique<AddNote>(clip, late)).ok());
    }

    Harness harness;
    ClipId clip{ClipId::generate()};
    NoteId first{NoteId::generate()};
    NoteId second{NoteId::generate()};
};

// One of each command that declares it reaches notes alone.
std::vector<std::unique_ptr<Command>> notesCommands(const Row& row)
{
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(Harness::addNote(row.clip, NoteId::generate(), 72));
    commands.push_back(std::make_unique<RemoveNote>(row.clip, row.first));
    commands.push_back(std::make_unique<MoveNote>(row.clip, row.first, 62, 2.0));
    commands.push_back(std::make_unique<ResizeNote>(row.clip, row.first, 1.0));
    commands.push_back(std::make_unique<SetNoteVelocity>(row.clip, row.first, 40));
    commands.push_back(std::make_unique<QuantizeNotes>(row.clip, std::vector<NoteId>{row.second}, 0.25));
    commands.push_back(std::make_unique<TransposeNotes>(row.clip, std::vector<NoteId>{row.first}, 12));
    return commands;
}

// The project as a value, without what a strip of the mixer holds: what a
// command that reaches the mix alone must leave exactly as it was.
Value withoutMix(const Value& value)
{
    static const std::vector<std::string> mixKeys{
        "volumeDb", "pan", "muted", "soloed", "sends", "output", "plugins", "role"};
    if (const auto* object = value.asObject(); object != nullptr)
    {
        Value::Object kept;
        for (const auto& [key, member] : *object)
            if (std::find(mixKeys.begin(), mixKeys.end(), key) == mixKeys.end())
                kept.emplace_back(key, withoutMix(member));
        return Value::object(std::move(kept));
    }
    if (const auto* array = value.asArray(); array != nullptr)
    {
        Value::Array kept;
        for (const auto& item : *array)
            kept.push_back(withoutMix(item));
        return Value::array(std::move(kept));
    }
    return value;
}

// A track with a bus to send to, a plugin and an internal effect on it.
struct Strip
{
    Strip()
    {
        REQUIRE(harness.bus.execute(std::make_unique<AddBus>(bus, "Réverbe")).ok());
        REQUIRE(harness.bus.execute(std::make_unique<SetTrackSend>(harness.trackId, bus, -12.0)).ok());
        PluginInstance hosted{};
        hosted.id = plugin;
        hosted.ref = PluginRef{std::string{PluginRef::vst3Format}, "uid", "Synthé"};
        REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, hosted, 0)).ok());
        PluginInstance eq{};
        eq.id = effect;
        eq.ref =
            PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::equaliser}, "Égaliseur"};
        REQUIRE(harness.bus.execute(std::make_unique<InsertPlugin>(harness.trackId, eq, 1)).ok());
    }

    Harness harness;
    TrackId bus{TrackId::generate()};
    PluginId plugin{PluginId::generate()};
    PluginId effect{PluginId::generate()};
};

// One of each command that declares it reaches the mix alone.
std::vector<std::unique_ptr<Command>> mixCommands(const Strip& strip)
{
    const auto track = strip.harness.trackId;
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<SetTrackVolume>(track, -6.0));
    commands.push_back(std::make_unique<SetTrackPan>(track, 0.5));
    commands.push_back(std::make_unique<SetTrackMuted>(track, true));
    commands.push_back(std::make_unique<SetTrackSolo>(track, true));
    commands.push_back(std::make_unique<SetTrackSend>(track, strip.bus, -3.0));
    commands.push_back(std::make_unique<RemoveTrackSend>(track, strip.bus));
    commands.push_back(std::make_unique<SetTrackOutput>(track, strip.bus));
    PluginInstance more{};
    more.id = PluginId::generate();
    more.ref =
        PluginRef{std::string{PluginRef::internalFormat}, std::string{internal::compressor}, "Compresseur"};
    commands.push_back(std::make_unique<InsertPlugin>(track, more, 2));
    commands.push_back(std::make_unique<RemovePlugin>(strip.plugin));
    commands.push_back(std::make_unique<SetPluginBypassed>(strip.plugin, true));
    commands.push_back(
        std::make_unique<SetPluginParameter>(strip.effect, std::string{internal::lowGain}, -3.0));
    commands.push_back(std::make_unique<CapturePluginState>(
        strip.plugin, StateBlobRef{std::string(BlobRef::digestLength, 'a'), 12}));
    return commands;
}

struct LastReceipt final : BusObserver
{
    void onExecuted(const Receipt& receipt) override { last = receipt; }
    void onCoalesced(const Receipt& receipt) override { last = receipt; }
    void onUndone(const Receipt& receipt) override { last = receipt; }
    void onRedone(const Receipt& receipt) override { last = receipt; }

    Receipt last{};
};

} // namespace

TEST_CASE("a command that reaches notes alone changes the notes and nothing else")
{
    const Row row;
    for (const auto& command : notesCommands(row))
    {
        CAPTURE(std::string{command->type()});
        CHECK(command->reach() == Reach::notes);

        auto after = row.harness.state;
        REQUIRE(command->apply(after).ok());
        CHECK(json::write(after.toValue()) != json::write(row.harness.state.toValue())); // it did something
        CHECK(json::write(withoutNotes(after.toValue())) ==
              json::write(withoutNotes(row.harness.state.toValue())));
    }
}

TEST_CASE("every note command of the registry is in the list the test above applies")
{
    const Row row;
    std::vector<std::string> tested;
    for (const auto& command : notesCommands(row))
        tested.emplace_back(command->type());

    for (const auto& type : row.harness.registry.types())
    {
        if (!type.starts_with("note."))
            continue;
        CAPTURE(type);
        CHECK(std::find(tested.begin(), tested.end(), type) != tested.end());
    }
}

TEST_CASE("the bus reports the reach of what it did, and of a whole entry it undoes")
{
    Row row;
    LastReceipt seen;
    static_cast<void>(row.harness.bus.addObserver(seen));

    REQUIRE(row.harness.bus.execute(std::make_unique<MoveNote>(row.clip, row.first, 61, 0.0)).ok());
    CHECK(seen.last.reach == Reach::notes);

    REQUIRE(row.harness.bus.execute(row.harness.setVolume(-3.0)).ok());
    CHECK(seen.last.reach == Reach::mix);

    // A group whose first command is a note, and the second is not: its undo
    // is not notes alone.
    std::vector<std::unique_ptr<Command>> mixed;
    mixed.push_back(std::make_unique<SetNoteVelocity>(row.clip, row.first, 20));
    mixed.push_back(row.harness.setVolume(-6.0));
    GroupOptions group{};
    group.label = "mixte";
    REQUIRE(row.harness.bus.executeGroup(std::move(mixed), group).ok());
    REQUIRE(row.harness.bus.undo().ok());
    CHECK(seen.last.reach == Reach::anything);

    std::vector<std::unique_ptr<Command>> notes;
    notes.push_back(std::make_unique<SetNoteVelocity>(row.clip, row.first, 20));
    notes.push_back(std::make_unique<TransposeNotes>(row.clip, std::vector<NoteId>{row.second}, 1));
    group.label = "notes";
    REQUIRE(row.harness.bus.executeGroup(std::move(notes), group).ok());
    REQUIRE(row.harness.bus.undo().ok());
    CHECK(seen.last.reach == Reach::notes);
    REQUIRE(row.harness.bus.redo().ok());
    CHECK(seen.last.reach == Reach::notes);
}

TEST_CASE("a command that reaches the mix changes the strips of the mixer and nothing else")
{
    const Strip strip;
    for (const auto& command : mixCommands(strip))
    {
        CAPTURE(std::string{command->type()});
        CHECK(command->reach() == Reach::mix);

        auto after = strip.harness.state;
        REQUIRE(command->apply(after).ok());
        CHECK(json::write(after.toValue()) != json::write(strip.harness.state.toValue())); // it did something
        CHECK(json::write(withoutMix(after.toValue())) ==
              json::write(withoutMix(strip.harness.state.toValue())));
    }
}

TEST_CASE("a group of mix commands reaches the mix, and so does its undo")
{
    Strip strip;
    LastReceipt seen;
    static_cast<void>(strip.harness.bus.addObserver(seen));

    std::vector<std::unique_ptr<Command>> mix;
    mix.push_back(std::make_unique<SetTrackVolume>(strip.harness.trackId, -4.0));
    mix.push_back(std::make_unique<SetPluginParameter>(strip.effect, std::string{internal::mid1Gain}, -2.0));
    GroupOptions group{};
    group.label = "mixage";
    REQUIRE(strip.harness.bus.executeGroup(std::move(mix), group).ok());
    CHECK(seen.last.reach == Reach::mix);
    REQUIRE(strip.harness.bus.undo().ok());
    CHECK(seen.last.reach == Reach::mix);
}
