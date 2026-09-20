#include "daw/persistence/Database.h"

#include <utility>

#include <sqlite3.h>

namespace daw::persistence
{
namespace
{

using domain::ErrorCode;
using domain::fail;

domain::Error storageFailure(sqlite3* connection, std::string_view what)
{
    std::string message{what};
    if (connection != nullptr)
    {
        message += ": ";
        message += sqlite3_errmsg(connection);
    }
    return domain::Error{ErrorCode::storageError, std::move(message)};
}

} // namespace

// ---------------------------------------------------------------------------
// Statement
// ---------------------------------------------------------------------------

Statement::Statement(sqlite3* connection, sqlite3_stmt* statement) noexcept
    : connection_{connection}
    , statement_{statement}
{
}

Statement::~Statement()
{
    if (statement_ != nullptr)
        sqlite3_finalize(statement_);
}

Statement::Statement(Statement&& other) noexcept
    : connection_{other.connection_}
    , statement_{other.statement_}
{
    other.connection_ = nullptr;
    other.statement_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept
{
    if (this != &other)
    {
        if (statement_ != nullptr)
            sqlite3_finalize(statement_);

        connection_ = other.connection_;
        statement_ = other.statement_;
        other.connection_ = nullptr;
        other.statement_ = nullptr;
    }
    return *this;
}

domain::Result<void> Statement::bind(int index, std::string_view text)
{
    if (statement_ == nullptr)
        return fail(ErrorCode::storageError, "binding on a statement that is not prepared");

    const auto bound =
        sqlite3_bind_text(statement_, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
    if (bound != SQLITE_OK)
        return storageFailure(connection_, "cannot bind a text value");

    return {};
}

domain::Result<void> Statement::bind(int index, std::int64_t value)
{
    if (statement_ == nullptr)
        return fail(ErrorCode::storageError, "binding on a statement that is not prepared");

    if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK)
        return storageFailure(connection_, "cannot bind an integer value");

    return {};
}

domain::Result<void> Statement::bindNull(int index)
{
    if (statement_ == nullptr)
        return fail(ErrorCode::storageError, "binding on a statement that is not prepared");

    if (sqlite3_bind_null(statement_, index) != SQLITE_OK)
        return storageFailure(connection_, "cannot bind a null value");

    return {};
}

domain::Result<bool> Statement::step()
{
    if (statement_ == nullptr)
        return fail(ErrorCode::storageError, "stepping a statement that is not prepared");

    const auto status = sqlite3_step(statement_);
    if (status == SQLITE_ROW)
        return true;
    if (status == SQLITE_DONE)
        return false;

    return storageFailure(connection_, "the database refused a statement");
}

domain::Result<void> Statement::reset()
{
    if (statement_ == nullptr)
        return fail(ErrorCode::storageError, "resetting a statement that is not prepared");

    if (sqlite3_reset(statement_) != SQLITE_OK)
        return storageFailure(connection_, "cannot reset a statement");

    sqlite3_clear_bindings(statement_);
    return {};
}

std::string Statement::columnText(int index) const
{
    if (statement_ == nullptr)
        return {};

    const auto* text = sqlite3_column_text(statement_, index);
    if (text == nullptr)
        return {};

    const auto size = sqlite3_column_bytes(statement_, index);
    return std::string{reinterpret_cast<const char*>(text), static_cast<std::size_t>(size)};
}

std::int64_t Statement::columnInt(int index) const
{
    if (statement_ == nullptr)
        return 0;

    return sqlite3_column_int64(statement_, index);
}

bool Statement::isNull(int index) const
{
    return statement_ == nullptr || sqlite3_column_type(statement_, index) == SQLITE_NULL;
}

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

Database::Database(sqlite3* connection) noexcept
    : connection_{connection}
{
}

Database::~Database()
{
    if (connection_ != nullptr)
        sqlite3_close(connection_);
}

Database::Database(Database&& other) noexcept
    : connection_{other.connection_}
{
    other.connection_ = nullptr;
}

Database& Database::operator=(Database&& other) noexcept
{
    if (this != &other)
    {
        if (connection_ != nullptr)
            sqlite3_close(connection_);

        connection_ = other.connection_;
        other.connection_ = nullptr;
    }
    return *this;
}

domain::Result<Database> Database::open(const std::filesystem::path& file)
{
    sqlite3* connection = nullptr;

    // u8 form: a project can live under a path with accents, and SQLite reads
    // its file names as UTF-8 on every platform, Windows included.
    const auto utf8 = file.u8string();
    const auto status = sqlite3_open_v2(reinterpret_cast<const char*>(utf8.c_str()),
                                        &connection,
                                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                        nullptr);
    if (status != SQLITE_OK)
    {
        auto error = storageFailure(connection, "cannot open " + file.string());
        sqlite3_close(connection);
        return error;
    }

    Database database{connection};

    // WAL is the decision of the week, and it is set on the file, not on the
    // connection: a project carries its own journal mode.
    if (auto walMode = database.queryInt("PRAGMA journal_mode = WAL"); !walMode)
        return walMode.error();

    if (auto pragmas = database.execute("PRAGMA synchronous = NORMAL;"
                                        "PRAGMA foreign_keys = ON;"
                                        "PRAGMA busy_timeout = 5000;");
        !pragmas)
        return pragmas.error();

    return database;
}

domain::Result<void> Database::execute(std::string_view sql)
{
    if (connection_ == nullptr)
        return fail(ErrorCode::storageError, "the database is not open");

    const std::string statement{sql};
    char* message = nullptr;
    if (sqlite3_exec(connection_, statement.c_str(), nullptr, nullptr, &message) != SQLITE_OK)
    {
        std::string text = message != nullptr ? message : "the database refused a statement";
        sqlite3_free(message);
        return fail(ErrorCode::storageError, std::move(text));
    }

    return {};
}

domain::Result<Statement> Database::prepare(std::string_view sql)
{
    if (connection_ == nullptr)
        return fail(ErrorCode::storageError, "the database is not open");

    sqlite3_stmt* statement = nullptr;
    const auto status =
        sqlite3_prepare_v2(connection_, sql.data(), static_cast<int>(sql.size()), &statement, nullptr);
    if (status != SQLITE_OK)
        return storageFailure(connection_, "cannot prepare a statement");

    return Statement{connection_, statement};
}

domain::Result<std::int64_t> Database::queryInt(std::string_view sql)
{
    auto statement = prepare(sql);
    if (!statement)
        return statement.error();

    auto row = statement.value().step();
    if (!row)
        return row.error();

    if (!row.value())
        return fail(ErrorCode::notFound, "the query returned no row");

    return statement.value().columnInt(0);
}

std::int64_t Database::lastInsertRowId() const noexcept
{
    return connection_ == nullptr ? 0 : sqlite3_last_insert_rowid(connection_);
}

domain::Result<void> Database::checkpoint()
{
    if (connection_ == nullptr)
        return {};

    // TRUNCATE empties the write-ahead log and removes it. Without it, a
    // project folder copied while the session runs would carry a -wal file
    // holding commands the database file does not have yet.
    if (sqlite3_wal_checkpoint_v2(connection_, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr) !=
        SQLITE_OK)
        return storageFailure(connection_, "cannot checkpoint the journal");

    return {};
}

domain::Result<void> Database::checkpointAndClose()
{
    if (connection_ == nullptr)
        return {};

    if (auto emptied = checkpoint(); !emptied)
        return emptied;

    if (sqlite3_close(connection_) != SQLITE_OK)
    {
        auto error = storageFailure(connection_, "cannot close the database");
        return error;
    }

    connection_ = nullptr;
    return {};
}

// ---------------------------------------------------------------------------
// Transaction
// ---------------------------------------------------------------------------

Transaction::Transaction(Database& database)
    : database_{database}
{
}

Transaction::~Transaction()
{
    if (open_)
        static_cast<void>(database_.execute("ROLLBACK"));
}

domain::Result<void> Transaction::begin()
{
    if (open_)
        return fail(ErrorCode::conflict, "a transaction is already open");

    auto begun = database_.execute("BEGIN IMMEDIATE");
    if (!begun)
        return begun;

    open_ = true;
    return {};
}

domain::Result<void> Transaction::commit()
{
    if (!open_)
        return fail(ErrorCode::conflict, "no transaction to commit");

    auto committed = database_.execute("COMMIT");
    if (!committed)
        return committed;

    open_ = false;
    return {};
}

} // namespace daw::persistence
