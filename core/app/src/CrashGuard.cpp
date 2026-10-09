#include "CrashGuard.h"

#include "About.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>

namespace daw::app
{
namespace
{

// What the handler reads, set on the message thread. Fixed buffers: nothing
// to allocate or lock when the process is already failing.
std::array<char, 512> lastCommand{};
std::array<wchar_t, 1024> markerPath{};
std::array<wchar_t, 1024> reportsPath{};
std::atomic<bool> handling{false};

constexpr const char* markerName = "session-ouverte.json";

juce::String nowText()
{
    return juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S");
}

// The marker as it stands, with the crash written into it.
void markCrash(const juce::String& at, const juce::String& module, bool plugin, const juce::File& report)
{
    const juce::File marker{juce::String{markerPath.data()}};
    auto parsed = juce::JSON::parse(marker);
    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
    {
        parsed = juce::var{new juce::DynamicObject{}};
        object = parsed.getDynamicObject();
    }
    auto* crash = new juce::DynamicObject{};
    crash->setProperty("a", at);
    crash->setProperty("module", module);
    crash->setProperty("plugin", plugin);
    crash->setProperty("rapport", report.getFullPathName());
    object->setProperty("plantage", juce::var{crash});
    static_cast<void>(marker.replaceWithText(juce::JSON::toString(parsed)));
}

// A plugin of the person's: a VST3 or CLAP module, or a DLL inside a bundle.
bool isPluginModule(const juce::String& module)
{
    return module.endsWithIgnoreCase(".vst3") || module.endsWithIgnoreCase(".clap") ||
           module.containsIgnoreCase(".vst3\\") || module.containsIgnoreCase(".clap\\");
}

void writeReport(const juce::String& what, const juce::String& module, juce::uint64 offset, void* exception)
{
    const auto at = nowText();
    const juce::File reports{juce::String{reportsPath.data()}};
    static_cast<void>(reports.createDirectory());
    const auto stem = "plantage-" + juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    const auto report = reports.getChildFile(stem + ".txt");
    const auto plugin = isPluginModule(module);

    juce::StringArray lines;
    lines.add(About::banner());
    lines.add(juce::String::fromUTF8("plantage : ") + at);
    lines.add(what);
    if (module.isNotEmpty())
        lines.add("module : " + module + " + 0x" +
                  juce::String::toHexString(static_cast<juce::int64>(offset)) +
                  (plugin ? juce::String::fromUTF8(" (un plugin)") : juce::String{}));
    lines.add(juce::String::fromUTF8("dernière commande : ") +
              (lastCommand[0] != '\0' ? juce::String::fromUTF8(lastCommand.data()) : juce::String{"aucune"}));
    {
        const juce::File marker{juce::String{markerPath.data()}};
        const auto project = juce::JSON::parse(marker).getProperty("projet", {}).toString();
        lines.add("projet : " + (project.isNotEmpty() ? project : juce::String{"inconnu"}));
    }
    lines.add("pile :");
    lines.add(juce::SystemStats::getStackBacktrace());
    static_cast<void>(report.replaceWithText(lines.joinIntoString("\n")));

#if JUCE_WINDOWS
    // A small minidump, beside it: threads and stacks, not the memory.
    const auto dump = reports.getChildFile(stem + ".dmp");
    const auto file = CreateFileW(dump.getFullPathName().toWideCharPointer(),
                                  GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
    if (file != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION information{};
        information.ThreadId = GetCurrentThreadId();
        information.ExceptionPointers = static_cast<EXCEPTION_POINTERS*>(exception);
        information.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(),
                          GetCurrentProcessId(),
                          file,
                          MiniDumpNormal,
                          exception != nullptr ? &information : nullptr,
                          nullptr,
                          nullptr);
        CloseHandle(file);
    }
#else
    static_cast<void>(exception);
#endif

    markCrash(at, module, plugin, report);
    juce::Logger::writeToLog(lines.joinIntoString("\n"));
}

#if JUCE_WINDOWS
LONG WINAPI onUnhandled(EXCEPTION_POINTERS* information)
{
    if (handling.exchange(true))
        return EXCEPTION_EXECUTE_HANDLER;

    const auto* record = information != nullptr ? information->ExceptionRecord : nullptr;
    const auto address = record != nullptr ? record->ExceptionAddress : nullptr;
    juce::String module;
    juce::uint64 offset = 0;
    HMODULE handle = nullptr;
    if (address != nullptr && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                 static_cast<LPCWSTR>(address),
                                                 &handle) != FALSE)
    {
        std::array<wchar_t, MAX_PATH> name{};
        GetModuleFileNameW(handle, name.data(), static_cast<DWORD>(name.size()));
        module = juce::String{name.data()};
        offset = static_cast<juce::uint64>(reinterpret_cast<std::uintptr_t>(address) -
                                           reinterpret_cast<std::uintptr_t>(handle));
    }
    writeReport(
        "exception 0x" +
            juce::String::toHexString(
                static_cast<juce::int64>(record != nullptr ? record->ExceptionCode : 0u)) +
            juce::String::fromUTF8(" à ") +
            juce::String::toHexString(static_cast<juce::int64>(reinterpret_cast<std::uintptr_t>(address))),
        module,
        offset,
        information);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

[[noreturn]] void onTerminate()
{
    if (!handling.exchange(true))
        writeReport("std::terminate : une exception C++ n'a été rattrapée nulle part", {}, 0, nullptr);
    std::abort();
}

} // namespace

CrashGuard::CrashGuard(juce::File settings, juce::File reports)
    : settings_(std::move(settings))
    , reports_(std::move(reports))
{
    const auto marker = settings_.getChildFile(markerName).getFullPathName();
    std::wcsncpy(markerPath.data(), marker.toWideCharPointer(), markerPath.size() - 1);
    std::wcsncpy(reportsPath.data(), reports_.getFullPathName().toWideCharPointer(), reportsPath.size() - 1);
}

CrashGuard::~CrashGuard() = default;

std::optional<CrashGuard::Previous> CrashGuard::takePrevious()
{
    const auto marker = settings_.getChildFile(markerName);
    if (!marker.existsAsFile())
        return std::nullopt;
    const auto parsed = juce::JSON::parse(marker);
    static_cast<void>(marker.deleteFile());

    Previous previous;
    previous.project = juce::File{parsed.getProperty("projet", {}).toString()};
    previous.since = parsed.getProperty("depuis", {}).toString();
    const auto crash = parsed.getProperty("plantage", {});
    previous.crashed = crash.isObject();
    previous.at = crash.getProperty("a", {}).toString();
    previous.module = crash.getProperty("module", {}).toString();
    previous.plugin = static_cast<bool>(crash.getProperty("plugin", false));
    previous.report = juce::File{crash.getProperty("rapport", {}).toString()};
    return previous;
}

void CrashGuard::install()
{
#if JUCE_WINDOWS
    SetUnhandledExceptionFilter(onUnhandled);
#endif
    std::set_terminate(onTerminate);
}

void CrashGuard::sessionOpened(const juce::File& project)
{
    auto* object = new juce::DynamicObject{};
    object->setProperty("projet", project.getFullPathName());
    object->setProperty("depuis", nowText());
    object->setProperty("version", About::banner());
    static_cast<void>(settings_.createDirectory());
    static_cast<void>(
        settings_.getChildFile(markerName).replaceWithText(juce::JSON::toString(juce::var{object})));
}

void CrashGuard::sessionClosed()
{
    static_cast<void>(settings_.getChildFile(markerName).deleteFile());
}

void CrashGuard::remember(const char* what, const domain::Receipt& receipt)
{
    std::snprintf(lastCommand.data(),
                  lastCommand.size(),
                  "%s %s, %s",
                  what,
                  receipt.type.c_str(),
                  nowText().toRawUTF8());
}

void CrashGuard::onExecuted(const domain::Receipt& receipt)
{
    remember("exécuté", receipt);
}

void CrashGuard::onUndone(const domain::Receipt& receipt)
{
    remember("annulé", receipt);
}

void CrashGuard::onRedone(const domain::Receipt& receipt)
{
    remember("rétabli", receipt);
}

void CrashGuard::crashForTest()
{
    // Written through a volatile pointer so the compiler keeps it.
    volatile int* nowhere = nullptr;
    *nowhere = 26;
}

} // namespace daw::app
