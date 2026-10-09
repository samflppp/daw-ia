#pragma once

#include "daw/domain/command/BusObserver.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace daw::app
{

// A crash, caught and told (S26, decided on 9 October 2026).
//
// While a project is open, a marker sits in the machine's settings folder
// (session-ouverte.json): the project and since when. A normal close removes
// it. A crash leaves it, with what the handler wrote into it: the faulting
// module, and the report's path. The report (plantages/, beside it) holds the
// version, the exception, the module and its offset, the stack, the last
// command applied and the project; a minidump goes next to it. Nothing is sent
// anywhere.
//
// What is lost: nothing that went through the bus — each command is written
// in its own SQLite transaction before it returns. The next launch reads the
// marker and offers to reopen the project where it was.
class CrashGuard final : public domain::BusObserver
{
public:
    // What the last session left behind, read once at start.
    struct Previous
    {
        juce::File project;
        juce::String since;  // when it was opened
        bool crashed{false}; // the handler ran; false: killed, or the machine stopped
        juce::String at;     // when it crashed
        juce::String module; // the faulting module's file
        bool plugin{false};  // that module is a plugin of the person's
        juce::File report;
    };

    // `settings`: where the marker lives (the machine's settings, or a
    // check's). `reports`: where the reports and minidumps go.
    CrashGuard(juce::File settings, juce::File reports);
    ~CrashGuard() override;

    CrashGuard(const CrashGuard&) = delete;
    CrashGuard& operator=(const CrashGuard&) = delete;
    CrashGuard(CrashGuard&&) = delete;
    CrashGuard& operator=(CrashGuard&&) = delete;

    // The marker of the last session, if it did not close normally. Read,
    // then removed: it is said once.
    [[nodiscard]] std::optional<Previous> takePrevious();

    // Installs the handlers (Windows' unhandled exception filter, and
    // std::terminate).
    void install();

    void sessionOpened(const juce::File& project);
    void sessionClosed();

    void onExecuted(const domain::Receipt& receipt) override;
    void onUndone(const domain::Receipt& receipt) override;
    void onRedone(const domain::Receipt& receipt) override;

    // For a check only: an access violation, here and now.
    static void crashForTest();

    [[nodiscard]] const juce::File& reportsFolder() const noexcept { return reports_; }

private:
    void remember(const char* what, const domain::Receipt& receipt);

    juce::File settings_;
    juce::File reports_;
};

} // namespace daw::app
