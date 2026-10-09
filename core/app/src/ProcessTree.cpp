#include "ProcessTree.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h, which it needs.
#include <tlhelp32.h>
#endif

#include <algorithm>
#include <iterator>
#include <map>

namespace daw::app::processes
{
namespace
{

#if JUCE_WINDOWS
// Every process alive, by its parent. A parent that died keeps its id in
// its children's entries: Python under a uv already killed is still found.
std::multimap<Id, Id> byParent()
{
    std::multimap<Id, Id> found;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return found;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (auto more = Process32FirstW(snapshot, &entry); more != FALSE;
         more = Process32NextW(snapshot, &entry))
        found.emplace(static_cast<Id>(entry.th32ParentProcessID), static_cast<Id>(entry.th32ProcessID));
    CloseHandle(snapshot);
    return found;
}

void killOne(Id id)
{
    if (const auto handle = OpenProcess(PROCESS_TERMINATE, FALSE, id); handle != nullptr)
    {
        TerminateProcess(handle, 1);
        CloseHandle(handle);
    }
}

// When a process started, or zero when it cannot be asked.
ULONGLONG startedAt(Id id)
{
    const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (handle == nullptr)
        return 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    ULONGLONG at = 0;
    if (GetProcessTimes(handle, &created, &exited, &kernel, &user) != FALSE)
        at = (static_cast<ULONGLONG>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    CloseHandle(handle);
    return at;
}

// A process id is reused once its process is gone: a child is one only if it
// started after its parent, and a cycle is cut at a depth no service reaches
// (uv, the launcher, Python, its workers).
void killUnder(const std::multimap<Id, Id>& tree, Id root, ULONGLONG rootStarted, int depth)
{
    if (depth > 8)
        return;
    const auto [first, last] = tree.equal_range(root);
    for (auto child = first; child != last; ++child)
    {
        const auto started = startedAt(child->second);
        if (child->second != root && started != 0 && started >= rootStarted)
            killUnder(tree, child->second, started, depth + 1);
    }
    killOne(root);
}
#endif

} // namespace

void dieWithThisProcess()
{
#if JUCE_WINDOWS
    // The handle is never closed: Windows closes it when this process ends,
    // and the job then kills what is in it.
    const auto job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == FALSE ||
        AssignProcessToJobObject(job, GetCurrentProcess()) == FALSE)
    {
        juce::Logger::writeToLog("processes: no job (" + juce::String{static_cast<int>(GetLastError())} +
                                 "): a service could outlive the DAW");
        CloseHandle(job);
    }
#endif
}

std::vector<Id> children()
{
    std::vector<Id> found;
#if JUCE_WINDOWS
    const auto self = static_cast<Id>(GetCurrentProcessId());
    const auto tree = byParent();
    const auto [first, last] = tree.equal_range(self);
    for (auto child = first; child != last; ++child)
        found.push_back(child->second);
    std::sort(found.begin(), found.end());
#endif
    return found;
}

Id startedBetween(const std::vector<Id>& before, const std::vector<Id>& after)
{
    std::vector<Id> started;
    std::set_difference(
        after.begin(), after.end(), before.begin(), before.end(), std::back_inserter(started));
    return started.size() == 1 ? started.front() : 0;
}

void killTree(Id root)
{
#if JUCE_WINDOWS
    if (root == 0)
        return;
    killUnder(byParent(), root, startedAt(root), 0);
#else
    static_cast<void>(root);
#endif
}

} // namespace daw::app::processes
