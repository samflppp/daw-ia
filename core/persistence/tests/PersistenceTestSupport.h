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

// Writes a project the way a build of the first eight weeks wrote one, and
// leaves it on schema 3.
//
// The rows are the ones an S8 binary produced, and not an imitation of them:
// track.add, clip.create_midi, note.add and track.set_volume all kept the
// payload they had, so running today's commands writes the same bytes. What
// the scenario adds is the version number on the meta table, put back to 3, so
// that the parent's open runs the 3 -> 4 step on a real file instead of on a
// file that was already current.
[[nodiscard]] int writeLegacySession(const std::filesystem::path& projectFolder);

// Opens a project, replays it, and adds what the channel rack of S9 wrote: a
// pattern laid by "+ Pattern", a row opened by the first lit cell, and a
// stroke of three more cells. Run on the output of writeLegacySession, it
// leaves a project the way a user of the first nine weeks would have: an S8
// clip migrated to a pattern, and an S9 pattern next to it.
[[nodiscard]] int extendAsRackSession(const std::filesystem::path& projectFolder);

// Writes a project the way the S16 build wrote one, and leaves it on schema
// 4: two tracks, a drum pattern laid twice and a bass pattern laid once, and a
// sample on the drum track. Every payload is one S16 wrote — no line anywhere,
// because lines did not exist — and the state it drops next to the project is
// the one S16 serialised. The identifiers are fixed (namespace s16 below), so
// the parent can name what it checks.
[[nodiscard]] int writeS16Session(const std::filesystem::path& projectFolder,
                                  const std::filesystem::path& stateFile);

// Opens a project, replays it, and writes its state and its undo depth as
// JSON: what another process sees when it opens the same folder.
[[nodiscard]] int dumpSession(const std::filesystem::path& projectFolder,
                              const std::filesystem::path& stateFile);

} // namespace scenarios

namespace s16
{
inline constexpr const char* kickTrack = "01JBWQ7Z00000000S16TRACK0K";
inline constexpr const char* bassTrack = "01JBWQ7Z00000000S16TRACK0B";
inline constexpr const char* drumPattern = "01JBWQ7Z0000000S16PATTERNA";
inline constexpr const char* bassPattern = "01JBWQ7Z0000000S16PATTERNB";
inline constexpr const char* drumAt0 = "01JBWQ7Z00000000S16P0SE0A0";
inline constexpr const char* drumAt4 = "01JBWQ7Z00000000S16P0SE0A4";
inline constexpr const char* bassAt0 = "01JBWQ7Z00000000S16P0SE0B0";
inline constexpr const char* sampleClip = "01JBWQ7Z00000000S16A0D10K8";
inline constexpr const char* drumRow = "01JBWQ7Z00000000S16C1P0A00";
inline constexpr const char* bassRow = "01JBWQ7Z00000000S16C1P0B00";
} // namespace s16

[[nodiscard]] std::string readTextFile(const std::filesystem::path& file);
[[nodiscard]] bool writeTextFile(const std::filesystem::path& file, const std::string& text);

} // namespace daw::testing
