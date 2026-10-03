#include "daw/persistence/ProjectStore.h"

#include "daw/domain/Timestamp.h"
#include "daw/domain/command/CommandEnvelope.h"
#include "daw/domain/serialization/Json.h"

#include <algorithm>
#include <charconv>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace daw::persistence
{
namespace
{

using domain::ErrorCode;
using domain::fail;

// --- the schema, one migration step per version ----------------------------
//
// The chain exists from version 1 on purpose. A migration path added the day
// the first column changes is a migration path that has never run, and the
// projects it was supposed to save are the ones it breaks.

struct Migration
{
    std::int64_t to;
    std::string_view sql;
};

constexpr std::string_view schemaV1 = R"sql(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS journal (
    seq              INTEGER PRIMARY KEY AUTOINCREMENT,
    kind             TEXT    NOT NULL CHECK (kind IN ('execute','coalesce','undo','redo')),
    command_id       TEXT    NOT NULL,
    at_micros        INTEGER NOT NULL,
    actor            TEXT    NOT NULL CHECK (actor IN ('user','copilot','generator')),
    context_digest   TEXT,
    context_bytes    INTEGER,
    type             TEXT,
    gesture_id       TEXT,
    payload          TEXT,
    envelope_version INTEGER NOT NULL,
    CHECK ((kind IN ('execute','coalesce')) = (payload IS NOT NULL)),
    CHECK ((kind IN ('execute','coalesce')) = (type IS NOT NULL))
);

CREATE INDEX IF NOT EXISTS journal_by_command ON journal (command_id);
)sql";

// There is no blob table here, and there will not be one: the BLAKE3 store in
// the blobs/ folder already is the content-addressed table. A digest column is
// plain text with no foreign key, exactly like a digest inside a payload.

// Version 2. Asking "what did the copilot change" walks the whole journal
// without it, and that question is the reason provenance exists.
constexpr std::string_view schemaV2 = R"sql(
CREATE INDEX IF NOT EXISTS journal_by_actor ON journal (actor, seq);
)sql";

// Version 3. A group is what makes several commands one history entry. It
// lives in two columns rather than inside the payload because the journal is
// read as a table as often as it is replayed: "what did that request change"
// has to be one SELECT.
//
// Both columns are nullable, and that is the whole migration: every row
// written before this version belonged to no group, which is exactly what a
// null says.
//
// The two columns are added in code rather than here, because SQLite has no
// ADD COLUMN IF NOT EXISTS and a migration that cannot be run twice is a
// migration that turns a repaired project into a broken one.
constexpr std::string_view schemaV3 = R"sql(
CREATE INDEX IF NOT EXISTS journal_by_group ON journal (group_id, seq);
)sql";

// Version 4. Nothing to change, and that is the point.
//
// The S9 model split what a clip held into a pattern and a placement. Not one
// column moved: this journal stores command payloads, and the payloads kept
// their shape — clip.create_midi still names a track, a clip, a start and a
// length, and only what it means changed. An older project therefore needs no
// conversion at all, and this build opens it by replaying it.
//
// What does change is the other direction. A project where a pattern is laid
// twice replays into a build of the S8 era as two patterns it cannot tell
// apart, silently. Nothing in a payload says otherwise, so the version number
// has to say it: the store already refuses a schema newer than itself, and
// this step is what makes that refusal happen instead of a wrong project.
constexpr std::string_view schemaV4 = R"sql(
SELECT 1;
)sql";

// Version 5. Nothing to change either, for the same reason as version 4.
//
// S17 made the playlist lines free: a placement and an audio clip name the
// line they are filed on, and four lane.* commands edit the lines. A journal
// written before keeps replaying untouched — a payload that names no line
// files the block on the line of its pattern or of its track, which is the
// playlist those projects showed.
//
// The other direction is the danger again. An S16 build would replay
// placement.move and read only the beat, dropping the line without a word,
// and a block the user filed on "Basse" would come back on its pattern's line.
// It already refuses a lane.create it does not know, but a project where
// blocks were only dragged between lines holds none. The number refuses it.
constexpr std::string_view schemaV5 = R"sql(
SELECT 1;
)sql";

// Version 6 (S20): nothing to migrate either. A plugin may now be an effect of
// the DAW ("internal") with parameters in its own units, and a track may carry
// a mix role. An S19 build would refuse the first in a payload it cannot
// validate and silently drop the second; the number makes it refuse the
// project, said once, at opening.
constexpr std::string_view schemaV6 = R"sql(
SELECT 1;
)sql";

constexpr Migration migrations[] = {
    {1, schemaV1}, {2, schemaV2}, {3, schemaV3}, {4, schemaV4}, {5, schemaV5}, {6, schemaV6}};

constexpr std::string_view insertSql =
    "INSERT INTO journal (kind, command_id, at_micros, actor, context_digest, context_bytes, "
    "type, gesture_id, payload, envelope_version, group_id, group_label) "
    "VALUES (?,?,?,?,?,?,?,?,?,?,?,?)";

constexpr std::string_view selectSql =
    "SELECT seq, kind, command_id, at_micros, actor, context_digest, context_bytes, type, "
    "gesture_id, payload, envelope_version, group_id, group_label FROM journal ORDER BY seq";

struct Row
{
    std::int64_t seq{0};
    std::string kind;
    std::string commandId;
    std::int64_t atMicros{0};
    std::string actor;
    std::string contextDigest;
    std::int64_t contextBytes{0};
    bool hasContext{false};
    std::string type;
    std::string gestureId;
    std::string payload;
    std::int64_t envelopeVersion{domain::CommandEnvelope::currentVersion};
    std::string groupId;
    std::string groupLabel;
};

[[nodiscard]] domain::Result<domain::Provenance> provenanceOf(const Row& row)
{
    auto actor = domain::parseActor(row.actor);
    if (!actor)
        return actor.error();

    domain::Provenance origin{};
    origin.actor = actor.value();

    if (row.hasContext)
    {
        domain::BlobRef context{};
        context.digest = row.contextDigest;
        context.byteCount = static_cast<std::uint64_t>(std::max<std::int64_t>(row.contextBytes, 0));
        if (auto valid = context.validate(); !valid)
            return valid.error();

        origin.context = context;
    }

    return origin;
}

// Rebuilds the envelope the bus replays. The columns are not a second truth:
// they are the envelope, spread out so that a later reader can ask "what did
// the copilot change last Tuesday" in SQL instead of parsing every payload.
[[nodiscard]] domain::Result<domain::Value> envelopeOf(const Row& row)
{
    auto id = domain::CommandId::parse(row.commandId);
    if (!id)
        return fail(id.error().code, "row " + std::to_string(row.seq) + ": " + id.error().message);

    auto payload = domain::json::read(row.payload);
    if (!payload)
        return fail(payload.error().code,
                    "row " + std::to_string(row.seq) + ": payload: " + payload.error().message);

    auto origin = provenanceOf(row);
    if (!origin)
        return fail(origin.error().code, "row " + std::to_string(row.seq) + ": " + origin.error().message);

    domain::CommandEnvelope envelope{};
    envelope.id = id.value();
    envelope.type = row.type;
    envelope.at = domain::Timestamp{row.atMicros};
    envelope.origin = origin.value();
    envelope.payload = std::move(payload).value();

    if (!row.gestureId.empty())
    {
        auto gesture = domain::GestureId::parse(row.gestureId);
        if (!gesture)
            return fail(gesture.error().code,
                        "row " + std::to_string(row.seq) + ": gesture: " + gesture.error().message);

        envelope.gesture = gesture.value();
    }

    if (!row.groupId.empty())
    {
        auto group = domain::GroupRef::fromValue(domain::Value::object(
            {{"id", domain::Value{row.groupId}}, {"label", domain::Value{row.groupLabel}}}));
        if (!group)
            return fail(group.error().code,
                        "row " + std::to_string(row.seq) + ": group: " + group.error().message);

        envelope.group = std::move(group).value();
    }

    return envelope.toValue();
}

} // namespace

ProjectStore::ProjectStore(ProjectFolder folder, Database database)
    : folder_{std::move(folder)}
    , database_{std::move(database)}
{
}

ProjectStore::~ProjectStore()
{
    stopRecording();
}

domain::Result<std::unique_ptr<ProjectStore>> ProjectStore::open(ProjectFolder folder)
{
    auto prepared = ProjectFolder::createOrOpen(folder.root());
    if (!prepared)
        return prepared.error();

    auto database = Database::open(prepared.value().databaseFile());
    if (!database)
        return database.error();

    std::unique_ptr<ProjectStore> store{
        new ProjectStore{std::move(prepared).value(), std::move(database).value()}};

    if (auto migrated = store->migrate(); !migrated)
        return migrated.error();

    if (auto identity = store->readIdentity(); !identity)
        return identity.error();

    return store;
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

domain::Result<std::int64_t> ProjectStore::readSchemaVersion()
{
    // A fresh file has no meta table at all, which is version zero and not an
    // error: creating a project is opening one that is not there yet.
    auto exists = database_.queryInt("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='meta'");
    if (!exists)
        return exists.error();

    if (exists.value() == 0)
        return std::int64_t{0};

    auto stored = readMeta("schema_version");
    if (!stored)
        return stored.error();

    if (stored.value().empty())
        return std::int64_t{0};

    std::int64_t version = 0;
    const auto& text = stored.value();
    const auto* first = text.data();
    const auto* last = first + text.size();
    if (std::from_chars(first, last, version).ec != std::errc{})
        return fail(ErrorCode::storageError, "the schema version is not a number: " + text);

    return version;
}

domain::Result<void> ProjectStore::migrate()
{
    auto current = readSchemaVersion();
    if (!current)
        return current.error();

    if (current.value() > schemaVersion)
        return fail(ErrorCode::storageError,
                    "this project was written by a newer version (schema " + std::to_string(current.value()) +
                        ", this build reads " + std::to_string(schemaVersion) + ")");

    for (const auto& migration : migrations)
    {
        if (migration.to <= current.value())
            continue;

        // One transaction per step: a migration that fails halfway leaves the
        // project on the version it had, never between two.
        Transaction transaction{database_};
        if (auto begun = transaction.begin(); !begun)
            return begun;

        // The columns come before the index that reads them.
        if (migration.to == 3)
        {
            if (auto added = addGroupColumns(); !added)
                return added;
        }

        if (auto applied = database_.execute(migration.sql); !applied)
            return applied;

        if (auto written = writeMeta("schema_version", std::to_string(migration.to)); !written)
            return written;

        if (migration.to == 1)
        {
            if (auto id = writeMeta("project_id", domain::Ulid::generate().toString()); !id)
                return id;

            if (auto created =
                    writeMeta("created_at_micros", std::to_string(domain::Timestamp::now().microsSinceEpoch));
                !created)
                return created;
        }

        if (auto committed = transaction.commit(); !committed)
            return committed;
    }

    versionOnDisk_ = schemaVersion;
    return {};
}

domain::Result<void> ProjectStore::addGroupColumns()
{
    for (const auto* column : {"group_id", "group_label"})
    {
        auto present = database_.queryInt("SELECT count(*) FROM pragma_table_info('journal') "
                                          "WHERE name = '" +
                                          std::string{column} + "'");
        if (!present)
            return present.error();

        if (present.value() > 0)
            continue;

        if (auto added = database_.execute("ALTER TABLE journal ADD COLUMN " + std::string{column} + " TEXT");
            !added)
            return added;
    }

    return {};
}

domain::Result<void> ProjectStore::readIdentity()
{
    auto id = readMeta("project_id");
    if (!id)
        return id.error();

    projectId_ = std::move(id).value();
    return {};
}

domain::Result<std::string> ProjectStore::readMeta(std::string_view key)
{
    auto statement = database_.prepare("SELECT value FROM meta WHERE key = ?");
    if (!statement)
        return statement.error();

    if (auto bound = statement.value().bind(1, key); !bound)
        return bound.error();

    auto row = statement.value().step();
    if (!row)
        return row.error();

    if (!row.value())
        return std::string{};

    return statement.value().columnText(0);
}

domain::Result<void> ProjectStore::writeMeta(std::string_view key, std::string_view value)
{
    auto statement = database_.prepare("INSERT INTO meta (key, value) VALUES (?,?) "
                                       "ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    if (!statement)
        return statement.error();

    if (auto bound = statement.value().bind(1, key); !bound)
        return bound;
    if (auto bound = statement.value().bind(2, value); !bound)
        return bound;

    auto row = statement.value().step();
    if (!row)
        return row.error();

    return {};
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void ProjectStore::startRecording(domain::CommandBus& bus)
{
    stopRecording();

    recordingBus_ = &bus;
    recordingToken_ = bus.addObserver(*this);
    recording_ = true;
}

void ProjectStore::stopRecording()
{
    if (recordingBus_ != nullptr)
        recordingBus_->removeObserver(recordingToken_);

    recordingBus_ = nullptr;
    recordingToken_ = domain::ObserverToken{};
    recording_ = false;
}

void ProjectStore::onExecuted(const domain::Receipt& receipt)
{
    if (receipt.policy == domain::HistoryPolicy::transient)
        return;

    record("execute", receipt);
}

void ProjectStore::onCoalesced(const domain::Receipt& receipt)
{
    record("coalesce", receipt);
}

void ProjectStore::onUndone(const domain::Receipt& receipt)
{
    record("undo", receipt);
}

void ProjectStore::onRedone(const domain::Receipt& receipt)
{
    record("redo", receipt);
}

void ProjectStore::record(std::string_view kind, const domain::Receipt& receipt)
{
    if (!recording_)
        return;

    // The first failure is the one worth reporting: the ones after it are its
    // consequences, and overwriting it would hide what actually broke.
    if (writeError_.code != ErrorCode::none)
        return;

    if (auto written = append(kind, receipt); !written)
        writeError_ = written.error();
}

domain::Result<void> ProjectStore::append(std::string_view kind, const domain::Receipt& receipt)
{
    const bool carriesPayload = kind == "execute" || kind == "coalesce";

    auto statement = database_.prepare(insertSql);
    if (!statement)
        return statement.error();

    auto& insert = statement.value();

    if (auto bound = insert.bind(1, kind); !bound)
        return bound;
    if (auto bound = insert.bind(2, receipt.id.toString()); !bound)
        return bound;
    if (auto bound = insert.bind(3, receipt.at.microsSinceEpoch); !bound)
        return bound;
    if (auto bound = insert.bind(4, domain::describe(receipt.origin.actor)); !bound)
        return bound;

    if (receipt.origin.context.has_value())
    {
        if (auto bound = insert.bind(5, receipt.origin.context->digest); !bound)
            return bound;
        if (auto bound = insert.bind(6, static_cast<std::int64_t>(receipt.origin.context->byteCount)); !bound)
            return bound;
    }
    else
    {
        if (auto bound = insert.bindNull(5); !bound)
            return bound;
        if (auto bound = insert.bindNull(6); !bound)
            return bound;
    }

    if (carriesPayload)
    {
        if (auto bound = insert.bind(7, receipt.type); !bound)
            return bound;
    }
    else if (auto bound = insert.bindNull(7); !bound)
    {
        return bound;
    }

    if (receipt.gesture.has_value())
    {
        if (auto bound = insert.bind(8, receipt.gesture->toString()); !bound)
            return bound;
    }
    else if (auto bound = insert.bindNull(8); !bound)
    {
        return bound;
    }

    if (carriesPayload)
    {
        if (auto bound = insert.bind(9, domain::json::write(receipt.payload)); !bound)
            return bound;
    }
    else if (auto bound = insert.bindNull(9); !bound)
    {
        return bound;
    }

    if (auto bound = insert.bind(10, domain::CommandEnvelope::currentVersion); !bound)
        return bound;

    if (receipt.group.has_value())
    {
        if (auto bound = insert.bind(11, receipt.group->id.toString()); !bound)
            return bound;
        if (auto bound = insert.bind(12, receipt.group->label); !bound)
            return bound;
    }
    else
    {
        if (auto bound = insert.bindNull(11); !bound)
            return bound;
        if (auto bound = insert.bindNull(12); !bound)
            return bound;
    }

    Transaction transaction{database_};
    if (auto begun = transaction.begin(); !begun)
        return begun;

    auto row = insert.step();
    if (!row)
        return row.error();

    return transaction.commit();
}

domain::Result<void> ProjectStore::status() const
{
    if (writeError_.code == ErrorCode::none)
        return {};

    return writeError_;
}

std::size_t ProjectStore::rowCount()
{
    auto count = database_.queryInt("SELECT count(*) FROM journal");
    if (!count)
        return 0;

    return static_cast<std::size_t>(std::max<std::int64_t>(count.value(), 0));
}

domain::Result<void> ProjectStore::save()
{
    auto written = status();
    auto emptied = database_.checkpoint();

    // A failed write matters more than a failed checkpoint: the checkpoint can
    // succeed on a journal that is missing a command.
    if (!written)
        return written;

    return emptied;
}

domain::Result<void> ProjectStore::close()
{
    auto written = status();
    auto closed = database_.checkpointAndClose();

    // A failed write is the more important of the two: the checkpoint can
    // succeed on a journal that is missing a command.
    if (!written)
        return written;

    return closed;
}

// ---------------------------------------------------------------------------
// Replay
// ---------------------------------------------------------------------------

domain::Result<ProjectStore::ReplayReport> ProjectStore::replayInto(domain::CommandBus& bus)
{
    auto statement = database_.prepare(selectSql);
    if (!statement)
        return statement.error();

    auto& select = statement.value();

    std::vector<Row> rows;
    while (true)
    {
        auto hasRow = select.step();
        if (!hasRow)
            return hasRow.error();

        if (!hasRow.value())
            break;

        Row row{};
        row.seq = select.columnInt(0);
        row.kind = select.columnText(1);
        row.commandId = select.columnText(2);
        row.atMicros = select.columnInt(3);
        row.actor = select.columnText(4);
        row.hasContext = !select.isNull(5);
        row.contextDigest = select.columnText(5);
        row.contextBytes = select.columnInt(6);
        row.type = select.columnText(7);
        row.gestureId = select.columnText(8);
        row.payload = select.columnText(9);
        row.envelopeVersion = select.columnInt(10);
        row.groupId = select.columnText(11);
        row.groupLabel = select.columnText(12);
        rows.push_back(std::move(row));
    }

    ReplayReport report{};
    report.rows = rows.size();

    // Fold each coalesce row into the entry it belongs to. The bus merges only
    // with the top of the undo stack, so the row before a coalesce is always
    // that entry; a journal where it is not is a journal that was written by
    // something else, and saying so is better than replaying a wrong history.
    std::vector<Row> events;
    events.reserve(rows.size());
    for (auto& row : rows)
    {
        if (row.kind == "coalesce")
        {
            if (events.empty() || events.back().commandId != row.commandId ||
                (events.back().kind != "execute" && events.back().kind != "coalesce"))
                return fail(ErrorCode::storageError,
                            "row " + std::to_string(row.seq) + ": a coalesced command follows no execution");

            events.back().payload = std::move(row.payload);
            events.back().type = std::move(row.type);
            continue;
        }

        events.push_back(std::move(row));
    }

    const bool wasRecording = recording_;
    recording_ = false;

    for (std::size_t index = 0; index < events.size(); ++index)
    {
        const auto& event = events[index];

        if (event.kind == "execute")
        {
            // The rows of a group are consecutive, because the bus applies its
            // commands back to back. They are replayed together or the project
            // would come back with the right state and a history of three
            // entries where the user left one.
            std::size_t last = index;
            if (!event.groupId.empty())
            {
                while (last + 1 < events.size() && events[last + 1].kind == "execute" &&
                       events[last + 1].groupId == event.groupId)
                    ++last;
            }

            std::vector<domain::Value> envelopes;
            envelopes.reserve(last - index + 1);
            for (std::size_t step = index; step <= last; ++step)
            {
                auto envelope = envelopeOf(events[step]);
                if (!envelope)
                {
                    recording_ = wasRecording;
                    return envelope.error();
                }

                envelopes.push_back(std::move(envelope).value());
            }

            if (event.groupId.empty())
            {
                auto replayed = bus.executeSerialized(envelopes.front());
                if (!replayed)
                {
                    recording_ = wasRecording;
                    return fail(replayed.error().code,
                                "row " + std::to_string(event.seq) + " (" + event.type +
                                    "): " + replayed.error().message);
                }
            }
            else
            {
                auto replayed = bus.executeSerializedGroup(envelopes);
                if (!replayed)
                {
                    recording_ = wasRecording;
                    return fail(replayed.error().code,
                                "row " + std::to_string(event.seq) + " (" + event.type +
                                    "): " + replayed.error().message);
                }
            }

            report.commands += last - index + 1;
            index = last;
            continue;
        }

        auto origin = provenanceOf(event);
        if (!origin)
        {
            recording_ = wasRecording;
            return origin.error();
        }

        auto moved = event.kind == "undo" ? bus.undo(origin.value()) : bus.redo(origin.value());
        if (!moved)
        {
            recording_ = wasRecording;
            return fail(moved.error().code,
                        "row " + std::to_string(event.seq) + " (" + event.kind +
                            "): " + moved.error().message);
        }

        if (event.kind == "undo")
            ++report.undone;
        else
            ++report.redone;
    }

    recording_ = wasRecording;
    return report;
}

} // namespace daw::persistence
