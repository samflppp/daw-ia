#pragma once

#include "daw/domain/Result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace daw::persistence
{

// The thinnest wrapper over SQLite that still keeps the two rules of this
// codebase: nothing throws, and every failure carries a message that names
// what failed. SQLite's own handles never leave this header.
//
// One writer, one connection. The connection belongs to the thread that opened
// it — the same thread that owns the Command Bus, which is what makes "a
// single writer" true by construction rather than by discipline. Readers of a
// later week (an arrangement branch view, a state extract for a model) open
// their own read-only connections; WAL is what lets them read while this one
// writes.

class Statement
{
public:
    Statement() noexcept = default;
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    // Bindings are one-based, as in SQLite. Text is copied: a bound
    // string_view never has to outlive the call.
    domain::Result<void> bind(int index, std::string_view text);
    domain::Result<void> bind(int index, std::int64_t value);
    domain::Result<void> bindNull(int index);

    // true when a row is available, false when the statement is done.
    [[nodiscard]] domain::Result<bool> step();
    domain::Result<void> reset();

    // Column readers are zero-based, as in SQLite. A NULL column reads as an
    // empty string or as zero, and isNull() tells the two apart.
    [[nodiscard]] std::string columnText(int index) const;
    [[nodiscard]] std::int64_t columnInt(int index) const;
    [[nodiscard]] bool isNull(int index) const;

private:
    friend class Database;

    Statement(sqlite3* connection, sqlite3_stmt* statement) noexcept;

    sqlite3* connection_{nullptr};
    sqlite3_stmt* statement_{nullptr};
};

class Database
{
public:
    Database() noexcept = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept;
    Database& operator=(Database&& other) noexcept;

    // Opens, creating the file if it is not there. WAL is set here: it is what
    // lets a reader read while the writer writes, and what keeps a crash from
    // leaving a half-written journal.
    [[nodiscard]] static domain::Result<Database> open(const std::filesystem::path& file);

    [[nodiscard]] bool isOpen() const noexcept { return connection_ != nullptr; }

    domain::Result<void> execute(std::string_view sql);
    [[nodiscard]] domain::Result<Statement> prepare(std::string_view sql);

    // Reads one integer, for the one-row PRAGMA and meta queries.
    [[nodiscard]] domain::Result<std::int64_t> queryInt(std::string_view sql);

    [[nodiscard]] std::int64_t lastInsertRowId() const noexcept;

    // Empties the write-ahead log into the database file and deletes it, so
    // the folder copies as one file plus its blobs.
    domain::Result<void> checkpointAndClose();

private:
    explicit Database(sqlite3* connection) noexcept;

    sqlite3* connection_{nullptr};
};

// A transaction that rolls back unless it is committed. Every write in the
// journal goes through one: a row that names a blob must never survive the
// failure of the row it belongs with.
class Transaction
{
public:
    explicit Transaction(Database& database);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = delete;
    Transaction& operator=(Transaction&&) = delete;

    [[nodiscard]] domain::Result<void> begin();
    [[nodiscard]] domain::Result<void> commit();

private:
    Database& database_;
    bool open_{false};
};

} // namespace daw::persistence
