#include "daw/persistence/ProjectFolder.h"

#include <system_error>
#include <utility>

namespace daw::persistence
{

using domain::ErrorCode;
using domain::fail;
using domain::Result;

ProjectFolder::ProjectFolder(std::filesystem::path root)
    : root_{std::move(root)}
{
}

Result<ProjectFolder> ProjectFolder::createOrOpen(std::filesystem::path root)
{
    if (root.empty())
        return fail(ErrorCode::invalidArgument, "the project folder has no path");

    ProjectFolder folder{std::move(root)};

    std::error_code code;
    std::filesystem::create_directories(folder.blobsFolder(), code);
    if (code)
        return fail(ErrorCode::storageError,
                    "cannot create the project folder " + folder.root_.string() + ": " + code.message());

    if (!std::filesystem::is_directory(folder.root_, code))
        return fail(ErrorCode::storageError, "not a project folder: " + folder.root_.string());

    return folder;
}

std::filesystem::path ProjectFolder::databaseFile() const
{
    return root_ / std::filesystem::path{databaseName};
}

std::filesystem::path ProjectFolder::blobsFolder() const
{
    return root_ / std::filesystem::path{blobsName};
}

std::string ProjectFolder::name() const
{
    auto stem = root_.filename().string();
    const auto dot = stem.rfind(suffix);
    if (dot != std::string::npos && dot + suffix.size() == stem.size())
        stem.erase(dot);
    return stem;
}

} // namespace daw::persistence
