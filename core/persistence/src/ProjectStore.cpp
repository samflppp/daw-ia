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
constexpr Migration migrations[] = {{1, schemaV1}};

constexpr std::string_view insertSql =
    "INSERT INTO journal (kind, command_id, at_micros, actor, context_digest, context_bytes, "
    "type, gesture_id, payload, envelope_version) VALUES (?,?,?,?,?,?,?,?,?,?)";

constexpr std::string_view selectSql =
    "SELECT seq, kind, command_id, at_micros, actor, context_digest, context_bytes, type, "
    "gesture_id, payload, envelope_version FROM journal ORDER BY seq";

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

    for (const auto& event : events)
    {
        if (event.kind == "execute")
        {
            auto envelope = envelopeOf(event);
            if (!envelope)
            {
                recording_ = wasRecording;
                return envelope.error();
            }

            auto executed = bus.executeSerialized(envelope.value());
            if (!executed)
            {
                recording_ = wasRecording;
                return fail(executed.error().code,
                            "row " + std::to_string(event.seq) + " (" + event.type +
                                "): " + executed.error().message);
            }

            ++report.commands;
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
