#include "PersistenceTestSupport.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/commands/TrackCommands.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/domain/serialization/Json.h"
#include "daw/persistence/Database.h"
#include "daw/persistence/ProjectStore.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

#include <doctest/doctest.h>

using namespace daw::domain;
using daw::persistence::Database;
using daw::persistence::ProjectFolder;
using daw::persistence::ProjectStore;
using daw::testing::readTextFile;
using daw::testing::runChildProcess;
using daw::testing::TemporaryFolder;

namespace
{

// A project reopened the way the application reopens one: fresh state, fresh
// registry, fresh bus, and the journal replayed into them.
struct Reopened
{
    ProjectState state;
    CommandRegistry registry{CommandRegistry::withBuiltinCommands()};
    CommandBus bus{state, registry};
    std::unique_ptr<ProjectStore> store;

    [[nodiscard]] std::string stateJson() const { return json::write(state.toValue()); }
};

[[nodiscard]] Result<ProjectStore::ReplayReport> reopen(Reopened& session,
                                                        const std::filesystem::path& folder)
{
    auto store = ProjectStore::open(ProjectFolder{folder});
    if (!store)
        return store.error();

    session.store = std::move(store).value();
    return session.store->replayInto(session.bus);
}

} // namespace

TEST_CASE("a project written by another process reopens identical")
{
    TemporaryFolder temporary{"roundtrip"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");

    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    // The writing process is gone. Everything from here comes off the disk.
    const auto expected = readTextFile(expectedFile);
    REQUIRE(!expected.empty());

    Reopened session;
    auto report = reopen(session, project);
    REQUIRE(report.ok());
    CHECK(report.value().commands > 0);
    CHECK(report.value().undone == 1);

    CHECK(session.stateJson() == expected);
}

TEST_CASE("the identifiers come back, not just the shape")
{
    TemporaryFolder temporary{"identifiers"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    Reopened session;
    REQUIRE(reopen(session, project).ok());

    const auto trackId = TrackId::parse("01JBWQ7Z0000000000000TRACK");
    REQUIRE(trackId.ok());
    REQUIRE(session.state.findTrack(trackId.value()) != nullptr);

    const auto clipId = ClipId::parse("01JBWQ7Z0000000000000CL1P0");
    REQUIRE(clipId.ok());
    const auto* clip = session.state.findClip(clipId.value());
    REQUIRE(clip != nullptr);
    CHECK(clip->notes.size() == 4);
}

TEST_CASE("the history still undoes after a reload")
{
    TemporaryFolder temporary{"undo-after-reload"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    Reopened session;
    REQUIRE(reopen(session, project).ok());

    // The last entry of the writing session was a track added by the copilot.
    const auto copilotTrack = TrackId::parse("01JBWQ7Z000000000000TRACK2");
    REQUIRE(copilotTrack.ok());
    REQUIRE(session.state.findTrack(copilotTrack.value()) != nullptr);

    REQUIRE(session.bus.canUndo());
    REQUIRE(session.bus.undo().ok());
    CHECK(session.state.findTrack(copilotTrack.value()) == nullptr);

    REQUIRE(session.bus.redo().ok());
    CHECK(session.state.findTrack(copilotTrack.value()) != nullptr);

    // And the fader sweep is still one entry, not twenty.
    std::size_t undone = 0;
    while (session.bus.canUndo())
    {
        REQUIRE(session.bus.undo().ok());
        ++undone;
    }
    // Two tracks, one clip, four notes, and the fader sweep: one entry, not
    // the twenty commands it took.
    CHECK(undone == 8);
}

TEST_CASE("a command that was undone stays in the journal and stays undone")
{
    TemporaryFolder temporary{"undone-stays-undone"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    Reopened session;
    REQUIRE(reopen(session, project).ok());

    // The generator's track was added and undone before the process died.
    const auto generatedTrack = TrackId::parse("01JBWQ7Z000000000000TRACK3");
    REQUIRE(generatedTrack.ok());
    CHECK(session.state.findTrack(generatedTrack.value()) == nullptr);

    // It is not lost, though: it is one redo away, and its row is on disk.
    REQUIRE(session.bus.canRedo());
    REQUIRE(session.bus.redo().ok());
    CHECK(session.state.findTrack(generatedTrack.value()) != nullptr);

    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());
    auto undoRows = database.value().queryInt("SELECT count(*) FROM journal WHERE kind = 'undo'");
    REQUIRE(undoRows.ok());
    CHECK(undoRows.value() == 1);
}

TEST_CASE("the provenance of each command is on disk, and queryable")
{
    TemporaryFolder temporary{"provenance"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());

    auto copilotRows = database.value().queryInt(
        "SELECT count(*) FROM journal WHERE actor = 'copilot' AND kind = 'execute'");
    REQUIRE(copilotRows.ok());
    CHECK(copilotRows.value() == 1);

    auto generatorRows = database.value().queryInt(
        "SELECT count(*) FROM journal WHERE actor = 'generator' AND kind = 'execute'");
    REQUIRE(generatorRows.ok());
    CHECK(generatorRows.value() == 1);

    // The context the copilot acted upon is named by digest, never by value.
    auto statement =
        database.value().prepare("SELECT context_digest, context_bytes FROM journal WHERE actor = 'copilot'");
    REQUIRE(statement.ok());
    auto row = statement.value().step();
    REQUIRE(row.ok());
    REQUIRE(row.value());
    CHECK(statement.value().columnText(0) == std::string(BlobRef::digestLength, 'b'));
    CHECK(statement.value().columnInt(1) == 2048);

    // And the replay gives it back to the bus, not just to SQL.
    Reopened session;
    REQUIRE(reopen(session, project).ok());
    const auto journal = session.bus.journal();
    std::size_t fromCopilot = 0;
    for (const auto& envelope : journal)
    {
        auto parsed = CommandEnvelope::fromValue(envelope);
        REQUIRE(parsed.ok());
        if (parsed.value().origin.actor == Actor::copilot)
        {
            ++fromCopilot;
            REQUIRE(parsed.value().origin.context.has_value());
            CHECK(parsed.value().origin.context->byteCount == 2048);
        }
    }
    CHECK(fromCopilot == 1);
}

TEST_CASE("a fader sweep keeps every value it went through, and replays the merged one")
{
    TemporaryFolder temporary{"coalesced"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());

    auto coalesced = database.value().queryInt("SELECT count(*) FROM journal WHERE kind = 'coalesce'");
    REQUIRE(coalesced.ok());
    CHECK(coalesced.value() == 19); // twenty frames, one execute and nineteen merges

    Reopened session;
    REQUIRE(reopen(session, project).ok());

    const auto trackId = TrackId::parse("01JBWQ7Z0000000000000TRACK");
    REQUIRE(trackId.ok());
    auto volume = session.state.trackVolume(trackId.value());
    REQUIRE(volume.ok());
    CHECK(volume.value() == doctest::Approx(-0.25 * 19.0)); // the last value of the sweep
}

TEST_CASE("a project is a folder: a database, blobs, and nothing left open")
{
    TemporaryFolder temporary{"folder"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    const ProjectFolder folder{project};
    CHECK(std::filesystem::exists(folder.databaseFile()));
    CHECK(std::filesystem::is_directory(folder.blobsFolder()));
    CHECK(folder.name() == "Projet");

    // The write-ahead log is checkpointed away on close, so what is copied is
    // the whole project and not a database missing its last commands.
    CHECK(!std::filesystem::exists(project / "project.db-wal"));

    // Copied whole, it opens and replays the same way.
    const auto copy = temporary.child("Copie.dawproj");
    std::filesystem::copy(project, copy, std::filesystem::copy_options::recursive);

    Reopened fromCopy;
    REQUIRE(reopen(fromCopy, copy).ok());
    CHECK(fromCopy.stateJson() == readTextFile(expectedFile));
}

TEST_CASE("a large project reopens, and the journal is what carries it")
{
    TemporaryFolder temporary{"large"};
    const auto project = temporary.child("Gros.dawproj");
    const auto expectedFile = temporary.child("expected.json");

    constexpr int noteCount = 5000;
    REQUIRE(
        runChildProcess(
            {"--child", "write-large", project.string(), expectedFile.string(), std::to_string(noteCount)}) ==
        0);

    const auto started = std::chrono::steady_clock::now();
    Reopened session;
    auto report = reopen(session, project);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    REQUIRE(report.ok());
    CHECK(report.value().commands == noteCount + 2);
    CHECK(session.stateJson() == readTextFile(expectedFile));

    MESSAGE("reopened " << report.value().commands << " commands in "
                        << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << " ms");

    // Not a performance budget, a sanity rail: a minute would mean the replay
    // is quadratic in the number of commands, which is a design failure and
    // not a slow machine.
    CHECK(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() < 60);
}

TEST_CASE("a truncated database is refused, and refuses in one piece")
{
    TemporaryFolder temporary{"truncated"};
    const auto project = temporary.child("Projet.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    const auto databaseFile = ProjectFolder{project}.databaseFile();
    const auto size = std::filesystem::file_size(databaseFile);
    REQUIRE(size > 4096);
    std::filesystem::resize_file(databaseFile, size / 2);

    Reopened session;
    auto report = reopen(session, project);
    REQUIRE(!report.ok());
    CHECK(report.error().code == ErrorCode::storageError);
    CHECK(!report.error().message.empty());
}

TEST_CASE("a file that is not a database is refused, not guessed at")
{
    TemporaryFolder temporary{"corrupt"};
    const auto project = temporary.child("Projet.dawproj");
    REQUIRE(ProjectFolder::createOrOpen(project).ok());

    const auto databaseFile = ProjectFolder{project}.databaseFile();
    REQUIRE(daw::testing::writeTextFile(databaseFile, std::string(8192, 'x')));

    auto store = ProjectStore::open(ProjectFolder{project});
    REQUIRE(!store.ok());
    CHECK(store.error().code == ErrorCode::storageError);
}

TEST_CASE("a project written by a newer build is refused instead of half-read")
{
    TemporaryFolder temporary{"from-the-future"};
    const auto project = temporary.child("Futur.dawproj");

    {
        auto store = ProjectStore::open(ProjectFolder{project});
        REQUIRE(store.ok());
        CHECK(store.value()->versionOnDisk() == ProjectStore::schemaVersion);
        CHECK(!store.value()->projectId().empty());
        REQUIRE(store.value()->close().ok());
    }

    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());
    REQUIRE(database.value().execute("UPDATE meta SET value = '99' WHERE key = 'schema_version'").ok());
    REQUIRE(database.value().checkpointAndClose().ok());

    auto store = ProjectStore::open(ProjectFolder{project});
    REQUIRE(!store.ok());
    CHECK(store.error().code == ErrorCode::storageError);
    CHECK(store.error().message.find("newer version") != std::string::npos);
}

TEST_CASE("an empty project gets a schema, an identity, and no rows")
{
    TemporaryFolder temporary{"empty"};
    const auto project = temporary.child("Vide.dawproj");

    auto store = ProjectStore::open(ProjectFolder{project});
    REQUIRE(store.ok());
    CHECK(store.value()->rowCount() == 0);

    const auto identity = store.value()->projectId();
    REQUIRE(store.value()->close().ok());

    // Reopening does not migrate again, and does not change who the project is.
    auto again = ProjectStore::open(ProjectFolder{project});
    REQUIRE(again.ok());
    CHECK(again.value()->projectId() == identity);
    CHECK(again.value()->versionOnDisk() == ProjectStore::schemaVersion);
}

TEST_CASE("rows written by a build that knew nothing of provenance replay as user")
{
    TemporaryFolder temporary{"envelope-v1"};
    const auto project = temporary.child("Ancien.dawproj");

    {
        auto store = ProjectStore::open(ProjectFolder{project});
        REQUIRE(store.ok());
        REQUIRE(store.value()->close().ok());
    }

    // What a v1 build would have left: an envelope version of 1, and an actor
    // column that only ever held "user" because nothing else existed.
    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());
    REQUIRE(database.value()
                .execute("INSERT INTO journal (kind, command_id, at_micros, actor, type, payload, "
                         "envelope_version) VALUES ('execute','01JBWQ7Z0000000000000TRACK',1758182400123456,"
                         "'user','track.add','{\"name\":\"Ancienne\",\"trackId\":"
                         "\"01JBWQ7Z0000000000000TRACK\",\"volumeDb\":0.0}',1)")
                .ok());
    REQUIRE(database.value().checkpointAndClose().ok());

    Reopened session;
    auto report = reopen(session, project);
    REQUIRE(report.ok());
    CHECK(report.value().commands == 1);

    const auto journal = session.bus.journal();
    REQUIRE(journal.size() == 1);
    auto envelope = CommandEnvelope::fromValue(journal.front());
    REQUIRE(envelope.ok());
    CHECK(envelope.value().origin.actor == Actor::user);
}

TEST_CASE("a project left at schema 1 is migrated, with its rows intact")
{
    TemporaryFolder temporary{"migration"};
    const auto project = temporary.child("Ancien.dawproj");
    const auto expectedFile = temporary.child("expected.json");
    REQUIRE(runChildProcess({"--child", "write", project.string(), expectedFile.string()}) == 0);

    // Put the project back where a build that only knew version 1 would have
    // left it: the index of version 2 gone, the version number with it.
    {
        auto database = Database::open(ProjectFolder{project}.databaseFile());
        REQUIRE(database.ok());
        REQUIRE(database.value().execute("DROP INDEX IF EXISTS journal_by_actor").ok());
        REQUIRE(database.value().execute("UPDATE meta SET value = '1' WHERE key = 'schema_version'").ok());
        REQUIRE(database.value().checkpointAndClose().ok());
    }

    // Opening migrates it, in one step, without touching a single row.
    Reopened session;
    auto report = reopen(session, project);
    REQUIRE_MESSAGE(report.ok(), report.error().message);
    CHECK(session.store->versionOnDisk() == ProjectStore::schemaVersion);
    CHECK(session.stateJson() == readTextFile(expectedFile));

    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());

    auto version = database.value().queryInt("SELECT CAST(value AS INTEGER) FROM meta "
                                             "WHERE key = 'schema_version'");
    REQUIRE(version.ok());
    CHECK(version.value() == ProjectStore::schemaVersion);

    auto index = database.value().queryInt("SELECT count(*) FROM sqlite_master "
                                           "WHERE type = 'index' AND name = 'journal_by_actor'");
    REQUIRE(index.ok());
    CHECK(index.value() == 1);

    // And the identity of the project is not re-engendered by a migration: it
    // is still the same project.
    auto identity = database.value().prepare("SELECT value FROM meta WHERE key = 'project_id'");
    REQUIRE(identity.ok());
    auto row = identity.value().step();
    REQUIRE(row.ok());
    REQUIRE(row.value());
    CHECK(identity.value().columnText(0) == session.store->projectId());
}

TEST_CASE("saving empties the write-ahead log without closing the project")
{
    TemporaryFolder temporary{"save"};
    const auto project = temporary.child("Projet.dawproj");

    auto store = ProjectStore::open(ProjectFolder{project});
    REQUIRE(store.ok());

    ProjectState state;
    const auto registry = CommandRegistry::withBuiltinCommands();
    CommandBus bus{state, registry};
    store.value()->startRecording(bus);

    const auto trackId = TrackId::generate();
    REQUIRE(bus.execute(std::make_unique<AddTrack>(trackId, "Piste", 0.0)).ok());

    const auto walFile = project / "project.db-wal";
    REQUIRE(std::filesystem::exists(walFile));
    CHECK(std::filesystem::file_size(walFile) > 0);

    // The log is emptied into the database file, not deleted: the file itself
    // only goes when the last connection closes.
    REQUIRE(store.value()->save().ok());
    CHECK(std::filesystem::file_size(walFile) == 0);

    // Still open, still recording: a save is not a close.
    REQUIRE(bus.execute(std::make_unique<AddTrack>(TrackId::generate(), "Autre", 0.0)).ok());
    CHECK(store.value()->rowCount() == 2);

    // And the copy taken right after the save carries both commands once the
    // second one has been saved too.
    REQUIRE(store.value()->save().ok());
    const auto copy = temporary.child("Copie.dawproj");
    std::filesystem::copy(project, copy, std::filesystem::copy_options::recursive);

    Reopened session;
    auto report = reopen(session, copy);
    REQUIRE(report.ok());
    CHECK(report.value().commands == 2);
}

TEST_CASE("a coalesced row that follows nothing is a malformed journal, and says so")
{
    TemporaryFolder temporary{"orphan-coalesce"};
    const auto project = temporary.child("Bancal.dawproj");

    {
        auto store = ProjectStore::open(ProjectFolder{project});
        REQUIRE(store.ok());
        REQUIRE(store.value()->close().ok());
    }

    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());
    REQUIRE(database.value()
                .execute("INSERT INTO journal (kind, command_id, at_micros, actor, type, payload, "
                         "envelope_version) VALUES ('coalesce','01JBWQ7Z0000000000000TRACK',17581824001234,"
                         "'user','track.add','{\"name\":\"Orpheline\",\"trackId\":"
                         "\"01JBWQ7Z0000000000000TRACK\",\"volumeDb\":0.0}',2)")
                .ok());
    REQUIRE(database.value().checkpointAndClose().ok());

    Reopened session;
    auto report = reopen(session, project);
    REQUIRE(!report.ok());
    CHECK(report.error().code == ErrorCode::storageError);
}

TEST_CASE("a group reopens as one entry, and one Ctrl+Z")
{
    TemporaryFolder temporary{"group"};
    const auto project = temporary.child("Projet.dawproj");
    REQUIRE(runChildProcess({"--child", "write-group", project.string()}) == 0);

    // Three commands were written, and the last two shared a group.
    auto database = Database::open(ProjectFolder{project}.databaseFile());
    REQUIRE(database.ok());

    auto rows = database.value().queryInt("SELECT count(*) FROM journal WHERE kind = 'execute'");
    REQUIRE(rows.ok());
    CHECK(rows.value() == 3);

    auto grouped = database.value().queryInt("SELECT count(*) FROM journal WHERE group_id IS NOT NULL");
    REQUIRE(grouped.ok());
    CHECK(grouped.value() == 2);

    auto distinctGroups =
        database.value().queryInt("SELECT count(DISTINCT group_id) FROM journal WHERE group_id IS NOT NULL");
    REQUIRE(distinctGroups.ok());
    CHECK(distinctGroups.value() == 1);
    REQUIRE(database.value().checkpointAndClose().ok());

    Reopened session;
    auto report = reopen(session, project);
    REQUIRE_MESSAGE(report.ok(), report.error().message);
    CHECK(report.value().commands == 3);

    const auto groupedTrack = TrackId::parse("01JBWQ7Z000000000000TRACK4");
    REQUIRE(groupedTrack.ok());
    const auto pluginId = PluginId::parse("01JBWQ7Z00000000000PG1N000");
    REQUIRE(pluginId.ok());
    REQUIRE(session.state.findTrack(groupedTrack.value()) != nullptr);
    REQUIRE(session.state.findPlugin(pluginId.value()) != nullptr);

    // Two entries, not three: the group came back as one.
    CHECK(session.bus.undoDepth() == 2);

    REQUIRE(session.bus.undo().ok());
    CHECK(session.state.findTrack(groupedTrack.value()) == nullptr);
    CHECK(session.state.findPlugin(pluginId.value()) == nullptr);
    CHECK(session.bus.undoDepth() == 1);

    // And the label the user's sentence left is still there, in the journal
    // the panel is rebuilt from.
    const auto journal = session.bus.journal();
    REQUIRE(journal.size() == 1);
    REQUIRE(session.bus.redo().ok());

    const auto restored = session.bus.journal();
    REQUIRE(restored.size() == 3);
    auto envelope = CommandEnvelope::fromValue(restored.back());
    REQUIRE(envelope.ok());
    REQUIRE(envelope.value().group.has_value());
    CHECK(envelope.value().group->label == "ajoute une piste Basse et mets-y Vital");
}
