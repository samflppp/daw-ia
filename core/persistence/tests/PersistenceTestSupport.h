#pragma once

#include "daw/domain/Result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace daw::testing
{

// The path of this very test binary, captured in main(). It is what lets a
// test relaunch itself as a child process: measuring persistence means closing
// a process and opening another one, not calling a loader twice in the same
// one. A project that only survives inside its writer's memory looks saved
// until the day it is not.
void setExecutablePath(const char* path);
[[nodiscard]] const std::filesystem::path& executablePath();

// Runs this binary again with the given arguments and waits for it. Returns
// the child's exit code, or -1 when it could not be launched.
[[nodiscard]] int runChildProcess(const std::vector<std::string>& arguments);

// A temporary folder that deletes itself. Names are unique per instance, so
// two tests can run side by side.
class TemporaryFolder
{
public:
    explicit TemporaryFolder(const std::string& label);
    ~TemporaryFolder();

    TemporaryFolder(const TemporaryFolder&) = delete;
    TemporaryFolder& operator=(const TemporaryFolder&) = delete;
    TemporaryFolder(TemporaryFolder&&) = delete;
    TemporaryFolder& operator=(TemporaryFolder&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path child(std::string_view name) const;

private:
    std::filesystem::path path_;
};

// What the child processes know how to do. Shared by the child entry point and
// by the tests that launch it, so a scenario is written once.
namespace scenarios
{

// Writes a project the way the application would: a track, a clip, notes, a
// fader gesture, a command from the copilot, one undone command. Drops the
// resulting ProjectState next to the project, as JSON, for the parent to
// compare against. Returns an exit code.
[[nodiscard]] int writeSession(const std::filesystem::path& projectFolder,
                               const std::filesystem::path& stateFile);

// Writes a project whose last action is a copilot group: one track and one
// plugin on it, asked for in one sentence. What the reopening has to rebuild
// is not only the state but the history — one entry, one Ctrl+Z.
[[nodiscard]] int writeGroupSession(const std::filesystem::path& projectFolder);

// Writes a project with `commands` note commands, for the volume test.
[[nodiscard]] int writeLargeSession(const std::filesystem::path& projectFolder,
                                    const std::filesystem::path& stateFile,
                                    int commands);

} // namespace scenarios

[[nodiscard]] std::string readTextFile(const std::filesystem::path& file);
[[nodiscard]] bool writeTextFile(const std::filesystem::path& file, const std::string& text);

} // namespace daw::testing
