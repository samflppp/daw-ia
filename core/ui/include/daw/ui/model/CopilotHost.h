#pragma once

#include <juce_events/juce_events.h>

#include <string>
#include <string_view>
#include <vector>

namespace daw::ui
{

// What the copilot panel is allowed to know.
//
// The copilot is a separate process, reached over a socket, watched by the
// application. None of that belongs in a panel: the panel has a field, a send
// button, a conversation and a state to show. It asks, and it is told.
//
// The same shape as PluginHost and WorkspaceHost, for the same reason: what is
// framework, process or machine stops at the application, and core/ui stays
// buildable without any of it.
//
// It is a ChangeBroadcaster because an answer arrives on a socket thread and a
// repaint belongs to the message thread. The broadcaster defers and collapses,
// so a burst of lines costs one repaint.
class CopilotHost : public juce::ChangeBroadcaster
{
public:
    enum class Status
    {
        // No process. Either it was never started, or it died and the
        // application said so — the DAW goes on either way.
        stopped,

        starting,

        // The process answers, and nothing is being asked of it.
        ready,

        // A request is running. The interface must stay alive throughout: the
        // model thinks for seconds, and a frozen window for seconds is a
        // broken application as far as anyone using it is concerned.
        working,

        // The process is there and cannot work — no API key, no network. The
        // reason is in statusMessage(), in French, because it is read.
        failed
    };

    // One line of the conversation. The copilot's answers are in French, and
    // so are its refusals: "I could not do that" is an answer, not a crash.
    struct Line
    {
        enum class From
        {
            user,
            copilot,
            failure
        };

        From from{From::user};
        std::string text;
    };

    CopilotHost() = default;
    ~CopilotHost() override = default;

    CopilotHost(const CopilotHost&) = delete;
    CopilotHost& operator=(const CopilotHost&) = delete;
    CopilotHost(CopilotHost&&) = delete;
    CopilotHost& operator=(CopilotHost&&) = delete;

    [[nodiscard]] virtual Status status() const = 0;

    // What to show next to the state. Empty when there is nothing to say.
    [[nodiscard]] virtual std::string statusMessage() const = 0;

    [[nodiscard]] virtual const std::vector<Line>& transcript() const = 0;

    // Sends a request. Returns at once: whatever happens next arrives through
    // the broadcaster.
    virtual void ask(std::string_view request) = 0;

    // Starts the process again after it died. The panel offers it; it never
    // does it on its own behind a click the user did not make.
    virtual void restart() = 0;
};

} // namespace daw::ui
