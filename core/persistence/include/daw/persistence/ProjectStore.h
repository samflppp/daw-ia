#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/command/BusObserver.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/persistence/Database.h"
#include "daw/persistence/ProjectFolder.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace daw::persistence
{

// The project on disk: an append-only journal in SQLite, next to the
// content-addressed store.
//
// Nothing is ever updated and nothing is ever deleted here. A command that was
// undone keeps its row, and the undo keeps its own; the undo stack is a view
// derived from the sequence, not a table. That is what makes an arrangement
// branch possible in S6: a branch is a pointer into the whole history, and an
// erased history has nowhere to point.
//
// Coalescing is written as a new row rather than as an update of the one
// before it, so a fader sweep leaves every value it went through. The loader
// keeps the last payload of an entry, which is the merged form the bus itself
// would replay. It is safe to fold them because the bus only ever merges with
// the top of the undo stack: any other event in between breaks the merge, so
// a coalesce row always follows the execute row it belongs to.
//
// Threading: the store observes the bus, so it is written from the bus thread
// and from there only. One writer, by construction.
class ProjectStore final : public domain::BusObserver
{
public:
    // The schema this build writes. A project carrying an older version is
    // migrated on open; a project carrying a newer one is refused, because
    // guessing what a future column meant is how projects get corrupted.
    //
    //   1  the journal, the meta table, the index by command
    //   2  an index by actor: "what did the copilot change" is the first
    //      question this journal will be asked, and it is the step that makes
    //      the migration chain run on a real project instead of only on an
    //      empty one.
    static constexpr std::int64_t schemaVersion = 2;

    ~ProjectStore() override;

    ProjectStore(const ProjectStore&) = delete;
    ProjectStore& operator=(const ProjectStore&) = delete;
    ProjectStore(ProjectStore&&) = delete;
    ProjectStore& operator=(ProjectStore&&) = delete;

    [[nodiscard]] static domain::Result<std::unique_ptr<ProjectStore>> open(ProjectFolder folder);

    [[nodiscard]] const ProjectFolder& folder() const noexcept { return folder_; }
    [[nodiscard]] const std::string& projectId() const noexcept { return projectId_; }
    [[nodiscard]] std::int64_t versionOnDisk() const noexcept { return versionOnDisk_; }

    // What a replay put back. rows counts what the journal held, including the
    // coalesce rows that were folded into the entry they belong to.
    struct ReplayReport
    {
        std::size_t rows{0};
        std::size_t commands{0};
        std::size_t undone{0};
        std::size_t redone{0};
    };

    // Replays the journal into a bus whose project is empty. The store does
    // not record while it replays: a replay is not a new history.
    [[nodiscard]] domain::Result<ReplayReport> replayInto(domain::CommandBus& bus);

    // From here on, every receipt of that bus becomes a row. The store
    // observes one bus at a time, and stops observing the moment it is asked
    // to: an observer left behind on a bus that outlives it is a crash.
    void startRecording(domain::CommandBus& bus);
    void stopRecording();

    // An observer cannot return a Result, so a write that failed is kept here
    // and reported by the next call. It is checked when a project is closed,
    // and it is the reason close() can say "this project was not fully saved"
    // instead of a silent loss.
    [[nodiscard]] domain::Result<void> status() const;

    // Moves the write-ahead log into the database file without closing
    // anything, so the folder can be copied while the session runs. It is what
    // an explicit "save" does: the commands were already written, one
    // transaction each, and this only moves them out of the -wal.
    [[nodiscard]] domain::Result<void> save();

    // Checkpoints the write-ahead log and closes the connection, so the folder
    // copies as one database file plus its blobs.
    [[nodiscard]] domain::Result<void> close();

    [[nodiscard]] std::size_t rowCount();

private:
    ProjectStore(ProjectFolder folder, Database database);

    void onExecuted(const domain::Receipt& receipt) override;
    void onCoalesced(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;

    void record(std::string_view kind, const domain::Receipt& receipt);
    [[nodiscard]] domain::Result<void> append(std::string_view kind, const domain::Receipt& receipt);

    [[nodiscard]] domain::Result<void> migrate();
    [[nodiscard]] domain::Result<void> readIdentity();
    [[nodiscard]] domain::Result<std::int64_t> readSchemaVersion();
    [[nodiscard]] domain::Result<std::string> readMeta(std::string_view key);
    [[nodiscard]] domain::Result<void> writeMeta(std::string_view key, std::string_view value);

    ProjectFolder folder_;
    Database database_;
    std::string projectId_;
    std::int64_t versionOnDisk_{0};
    domain::CommandBus* recordingBus_{nullptr};
    domain::ObserverToken recordingToken_{};
    bool recording_{false};
    domain::Error writeError_{};
};

} // namespace daw::persistence
