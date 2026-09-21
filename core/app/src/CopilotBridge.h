#pragma once

#include "daw/domain/command/CommandBus.h"
#include "daw/domain/command/CommandQueue.h"
#include "daw/domain/command/CommandRegistry.h"
#include "daw/domain/copilot/StateView.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/CopilotHost.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace juce
{
class ChildProcess;
class StreamingSocket;
} // namespace juce

namespace daw::app
{

// The copilot, seen from the application: a Python process, a local socket,
// and the rule that the bus belongs to the message thread.
//
// Who calls whom, and why it goes both ways. The DAW listens on 127.0.0.1 on
// an ephemeral port and passes that port to the process it launches, so the
// child needs no configuration of its own and two DAWs on one machine do not
// fight over a port. Once connected, both sides ask:
//
//   the copilot asks the DAW    state.get, clip.notes, plugins.find,
//                               tools.list, commands.execute
//   the DAW asks the copilot    copilot.ask, once per request typed by the user
//
// The frame is one JSON object per line, UTF-8. Content-Length framing would
// buy nothing here: every message is small and a newline cannot appear inside
// a JSON string without being escaped.
//
// Threads. The socket is read by this object's own thread, and that thread
// never touches the bus or the project: commands go through a CommandQueue and
// are run by the message thread, reads are run by the message thread too and
// waited for. The process is watched by a timer, and its death is reported,
// never fatal: a DAW whose copilot died is a DAW.
class CopilotBridge final : public ui::CopilotHost,
                            private juce::Thread,
                            private juce::Timer,
                            private juce::AsyncUpdater
{
public:
    struct Wiring
    {
        domain::CommandBus& bus;
        domain::ProjectState& state;
        const domain::CommandRegistry& registry;

        // What this machine holds, asked for when the copilot wants it rather
        // than kept here: a scan can happen while the application runs.
        std::function<std::vector<domain::PluginRef>()> installedPlugins;
    };

    explicit CopilotBridge(Wiring wiring);
    ~CopilotBridge() override;

    CopilotBridge(const CopilotBridge&) = delete;
    CopilotBridge& operator=(const CopilotBridge&) = delete;
    CopilotBridge(CopilotBridge&&) = delete;
    CopilotBridge& operator=(CopilotBridge&&) = delete;

    // Opens the port, launches the process and starts watching it. Failures
    // are states, not exceptions: the panel says what happened.
    void start();
    void stop();

    // --- CopilotHost
    [[nodiscard]] Status status() const override;
    [[nodiscard]] std::string statusMessage() const override;
    [[nodiscard]] const std::vector<Line>& transcript() const override;
    void ask(std::string_view request) override;
    void restart() override;

private:
    // --- the socket thread
    void run() override;
    void readMessages();
    void handleMessage(const juce::String& line);
    void handleRequest(const domain::Value& message);
    void handleAnswer(const domain::Value& message);
    void send(const domain::Value& message);

    // --- the message thread
    void timerCallback() override;
    void handleAsyncUpdate() override;

    // Runs `job` on the message thread and waits for it. Used by the socket
    // thread for the reads: a project read from two threads is a race, even
    // when nothing is written.
    [[nodiscard]] domain::Result<domain::Value>
    onMessageThread(std::function<domain::Result<domain::Value>()> job);

    [[nodiscard]] domain::Result<domain::Value> readState() const;
    [[nodiscard]] domain::copilot::MachinePlugins machinePlugins() const;

    // --- state shared between the two threads
    void setStatus(Status status, std::string message);
    void addLine(Line::From from, std::string text);

    [[nodiscard]] juce::StringArray childCommand(int port) const;

    Wiring wiring_;
    // Rebuilt at every start: close() is final by design, so a queue that was
    // closed when the process died cannot serve the process that replaces it.
    std::unique_ptr<domain::CommandQueue> queue_;

    std::unique_ptr<juce::StreamingSocket> listener_;
    std::unique_ptr<juce::StreamingSocket> connection_;
    std::unique_ptr<juce::ChildProcess> process_;
    int port_{0};

    mutable std::mutex mutex_;
    Status status_{Status::stopped};
    std::string statusMessage_;
    std::vector<Line> transcript_;

    std::mutex writeMutex_;
    std::atomic<std::int64_t> nextRequestId_{1};
    std::atomic<bool> connected_{false};
    juce::String incoming_;
};

} // namespace daw::app
