#pragma once

#include "daw/domain/Result.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace daw::persistence
{

// A project is a folder, and copying that folder copies the project:
//
//   Mon projet.dawproj/
//       project.db        the journal, in SQLite WAL mode
//       blobs/aa/aabb...  the content-addressed store
//
// The blobs live inside the project and no longer under the user's application
// data. It costs the deduplication between two projects that captured the same
// plugin state; it buys a project that survives a copy onto a USB stick, which
// is the whole point of "a project is a folder".
//
// project.db-wal and project.db-shm appear next to the database while it is
// open. They are transient: ProjectStore::close() checkpoints and removes
// them, so a closed project is one file plus a folder of blobs.
class ProjectFolder
{
public:
    static constexpr std::string_view suffix = ".dawproj";
    static constexpr std::string_view databaseName = "project.db";
    static constexpr std::string_view blobsName = "blobs";

    explicit ProjectFolder(std::filesystem::path root);

    // Creates the folder and its blobs directory if they are not there. An
    // existing folder is never emptied: opening a project is not creating one.
    [[nodiscard]] static domain::Result<ProjectFolder> createOrOpen(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
    [[nodiscard]] std::filesystem::path databaseFile() const;
    [[nodiscard]] std::filesystem::path blobsFolder() const;

    [[nodiscard]] std::string name() const;

private:
    std::filesystem::path root_;
};

} // namespace daw::persistence
