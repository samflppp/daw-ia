#include "QuitWatchdog.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

namespace daw::app
{
namespace
{

std::atomic<const char*>& currentStep()
{
    static std::atomic<const char*> step{"start"};
    return step;
}

std::atomic<double>& armedAt()
{
    static std::atomic<double> at{0.0};
    return at;
}

// Not juce::Process::terminate: on Windows it is ExitProcess, which runs every
// DLL's detach, and a driver stuck in its own is one of the ways in.
[[noreturn]] void endProcessNow()
{
#if JUCE_WINDOWS
    TerminateProcess(GetCurrentProcess(), 2);
#endif
    std::_Exit(2);
}

} // namespace

void QuitWatchdog::step(const char* name) noexcept
{
    currentStep().store(name);
}

// The thread is detached and never told the teardown went well: a process
// that ends takes it along. Whatever comes after shutdown() — JUCE's objects
// deleted at shutdown, the statics — stays under the deadline too.
void QuitWatchdog::arm(const juce::File& logFile, int deadlineMs)
{
    armedAt().store(juce::Time::getMillisecondCounterHiRes());

    std::thread(
        [logFile, deadlineMs]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds{deadlineMs});

            // The log file is written directly: the logger may already be gone.
            static_cast<void>(logFile.appendText(juce::String("quit: stuck in \"") + currentStep().load() +
                                                 "\" after " + juce::String(deadlineMs) +
                                                 " ms; the process is ended here, the project was saved\n"));
            endProcessNow();
        })
        .detach();
}

void QuitWatchdog::done()
{
    currentStep().store("after shutdown");
    juce::Logger::writeToLog("quit: teardown in " +
                             juce::String(juce::Time::getMillisecondCounterHiRes() - armedAt().load(), 1) +
                             " ms");
}

} // namespace daw::app
