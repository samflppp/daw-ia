#include "CopilotBridge.h"

#include "daw/domain/copilot/Tools.h"
#include "daw/domain/serialization/Json.h"

#include <juce_core/juce_core.h>

#include <array>
#include <chrono>
#include <future>
#include <utility>

namespace daw::app
{
namespace
{

using domain::Value;

constexpr int firstPort = 49500;
constexpr int lastPort = 49599;

constexpr int readTimeoutMs = 200;
constexpr int watchIntervalMs = 1000;
constexpr int messageThreadTimeoutMs = 2000;

Value errorValue(std::string_view code, std::string message)
{
    return Value::object({{"code", Value{std::string{code}}}, {"message", Value{std::move(message)}}});
}

// Every failure the copilot can meet, said in French, because the panel shows
// it to whoever typed the request.
std::string describe(domain::ErrorCode code, const std::string& message)
{
    switch (code)
    {
    case domain::ErrorCode::unknownCommandType:
        return "Le copilote a demandé une action que ce logiciel ne connaît pas : " + message;
    case domain::ErrorCode::invalidPayload:
    case domain::ErrorCode::invalidArgument:
    case domain::ErrorCode::typeMismatch:
        return "Le copilote a envoyé une demande mal formée : " + message;
    case domain::ErrorCode::notFound:
        return "Le copilote a visé quelque chose qui n'existe pas : " + message;
    case domain::ErrorCode::conflict:
        return "Le projet a refusé : " + message;
    case domain::ErrorCode::wrongThread:
    case domain::ErrorCode::reentrantCall:
        return "Demande arrivée au mauvais moment, rien n'a été modifié : " + message;
    default:
        return "Le projet a refusé la demande du copilote : " + message;
    }
}

} // namespace

CopilotBridge::CopilotBridge(Wiring wiring)
    : juce::Thread{"copilot"}
    , wiring_{std::move(wiring)}
{
}

CopilotBridge::~CopilotBridge()
{
    stop();
}

// ---------------------------------------------------------------------------
// Life
// ---------------------------------------------------------------------------

void CopilotBridge::start()
{
    stop();

    setStatus(Status::starting, "Démarrage du copilote...");

    listener_ = std::make_unique<juce::StreamingSocket>();
    port_ = 0;
    for (int port = firstPort; port <= lastPort; ++port)
    {
        if (listener_->createListener(port, "127.0.0.1"))
        {
            port_ = port;
            break;
        }
    }

    if (port_ == 0)
    {
        listener_.reset();
        setStatus(Status::failed, "Aucun port local libre pour parler au copilote.");
        return;
    }

    process_ = std::make_unique<juce::ChildProcess>();
    const auto command = childCommand(port_);
    juce::Logger::writeToLog("copilot: launching " + command.joinIntoString(" "));
    if (!process_->start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        process_.reset();
        listener_.reset();
        setStatus(Status::failed,
                  "Le process du copilote n'a pas démarré : " + command.joinIntoString(" ").toStdString());
        return;
    }

    // The queue is drained on the message thread, and the socket thread is
    // what asks for it. The domain knows nothing of the AsyncUpdater behind.
    queue_ = std::make_unique<domain::CommandQueue>();
    queue_->onSubmitted([this] { triggerAsyncUpdate(); });

    startThread();
    startTimer(watchIntervalMs);
}

void CopilotBridge::stop()
{
    stopTimer();
    signalThreadShouldExit();

    // Closing the sockets unblocks the reading thread, which would otherwise
    // sit in its timeout for as long as the timeout lasts.
    if (connection_ != nullptr)
        connection_->close();
    if (listener_ != nullptr)
        listener_->close();

    stopThread(2000);

    if (queue_ != nullptr)
        queue_->close();

    if (process_ != nullptr)
    {
        process_->kill();
        process_.reset();
    }

    connection_.reset();
    listener_.reset();
    connected_ = false;
}

void CopilotBridge::restart()
{
    start();
}

juce::StringArray CopilotBridge::childCommand(int port) const
{
    // A whole command line, when the machine wants something else. Nothing in
    // here is a secret: the API key travels by environment, never by argument,
    // because arguments are readable by every process on the machine.
    const auto override = juce::SystemStats::getEnvironmentVariable("DAW_IA_COPILOT_COMMAND", {});
    if (override.isNotEmpty())
    {
        // fromTokens keeps the quotes it split on, and a path handed to a
        // process with its quotes still attached is a path that does not
        // exist.
        auto command = juce::StringArray::fromTokens(override, true);
        for (auto& argument : command)
            argument = argument.unquoted();

        command.add("--port");
        command.add(juce::String{port});
        return command;
    }

    auto servicesDirectory = juce::SystemStats::getEnvironmentVariable("DAW_IA_SERVICES_DIR", {});

    // The working directory first, then the folders above the binary. The
    // working directory alone was the rule until S10, and it held only for a
    // launch from the repository root: a double-click on the executable, or a
    // launch from anywhere else, started uv on a folder that does not exist
    // and the copilot died with code 2 before saying a word.
    if (servicesDirectory.isEmpty())
    {
        const auto isServices = [](const juce::File& folder)
        { return folder.getChildFile("pyproject.toml").existsAsFile(); };

        auto candidate = juce::File::getCurrentWorkingDirectory().getChildFile("services");
        for (auto folder =
                 juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
             !isServices(candidate) && folder.exists() && !folder.isRoot();
             folder = folder.getParentDirectory())
        {
            candidate = folder.getChildFile("services");
        }

        servicesDirectory = candidate.getFullPathName();
    }

    juce::StringArray command;
    command.add("uv");
    command.add("run");
    command.add("--project");
    command.add(servicesDirectory);
    command.add("daw-services");
    command.add("copilot");
    command.add("--port");
    command.add(juce::String{port});
    return command;
}

// ---------------------------------------------------------------------------
// The socket thread
// ---------------------------------------------------------------------------

void CopilotBridge::run()
{
    if (listener_ == nullptr)
        return;

    auto* accepted = listener_->waitForNextConnection();
    if (accepted == nullptr)
    {
        if (!threadShouldExit())
            setStatus(Status::failed, "Le copilote ne s'est pas connecté.");
        return;
    }

    connection_.reset(accepted);
    connected_ = true;
    setStatus(Status::ready, {});

    readMessages();

    connected_ = false;
}

void CopilotBridge::readMessages()
{
    std::array<char, 8192> buffer{};

    while (!threadShouldExit() && connection_ != nullptr && connection_->isConnected())
    {
        const auto ready = connection_->waitUntilReady(true, readTimeoutMs);
        if (ready < 0)
            break;

        if (ready == 0)
            continue;

        const auto read = connection_->read(buffer.data(), static_cast<int>(buffer.size()), false);
        if (read <= 0)
            break;

        incoming_ += juce::String::fromUTF8(buffer.data(), read);

        // One JSON object per line. Anything after the last newline is the
        // beginning of a message that has not arrived whole yet.
        for (auto newline = incoming_.indexOfChar('\n'); newline >= 0; newline = incoming_.indexOfChar('\n'))
        {
            const auto line = incoming_.substring(0, newline).trim();
            incoming_ = incoming_.substring(newline + 1);

            if (line.isNotEmpty())
                handleMessage(line);
        }
    }

    if (!threadShouldExit())
        setStatus(Status::stopped, "Le copilote s'est arrêté. Le projet est intact.");
}

void CopilotBridge::handleMessage(const juce::String& line)
{
    auto parsed = domain::json::read(line.toStdString());
    if (!parsed)
        return;

    if (parsed.value().contains("method"))
    {
        handleRequest(parsed.value());
        return;
    }

    handleAnswer(parsed.value());
}

void CopilotBridge::handleRequest(const Value& message)
{
    const auto method = message.stringAt("method");
    if (!method)
        return;

    const auto* idValue = message.find("id");
    const auto* params = message.find("params");
    const Value empty{};
    const auto& arguments = params != nullptr ? *params : empty;

    Value result{};
    Value failure{};

    if (method.value() == "state.get")
    {
        auto state = onMessageThread([this] { return readState(); });
        if (state)
            result = std::move(state).value();
        else
            failure = errorValue("state", state.error().message);
    }
    else if (method.value() == "tools.list")
    {
        auto tools = domain::copilot::toolsFor(wiring_.registry);
        if (tools)
            result = domain::copilot::toValue(tools.value());
        else
            failure = errorValue("tools", tools.error().message);
    }
    else if (method.value() == "clip.notes")
    {
        const auto clipText = arguments.stringAt("clipId");
        if (!clipText)
        {
            failure = errorValue("payload", clipText.error().message);
        }
        else
        {
            auto clipId = domain::ClipId::parse(clipText.value());
            if (!clipId)
            {
                failure = errorValue("payload", clipId.error().message);
            }
            else
            {
                auto notes = onMessageThread([this, id = clipId.value()]
                                             { return domain::copilot::clipNotes(wiring_.state, id); });
                if (notes)
                    result = std::move(notes).value();
                else
                    failure = errorValue("notFound", notes.error().message);
            }
        }
    }
    else if (method.value() == "plugins.find")
    {
        const auto query = arguments.stringAt("query");
        result = domain::copilot::findPlugins(machinePlugins(), query ? query.value() : std::string{});
    }
    else if (method.value() == "commands.execute")
    {
        domain::CommandQueue::Request request{};
        request.origin.actor = domain::Actor::copilot;

        if (const auto label = arguments.stringAt("label"); label)
            request.label = label.value();

        const auto* commands = arguments.find("commands");
        if (commands == nullptr || commands->asArray() == nullptr || commands->asArray()->empty())
        {
            failure = errorValue("payload", "commands must be a non-empty array");
        }
        else
        {
            for (const auto& command : *commands->asArray())
            {
                const auto type = command.stringAt("type");
                const auto* payload = command.find("payload");
                if (!type || payload == nullptr)
                {
                    failure = errorValue("payload", "each command needs a type and a payload");
                    break;
                }

                request.steps.push_back(domain::CommandQueue::Step{type.value(), *payload});
            }
        }

        if (failure.isNull())
        {
            if (queue_ == nullptr)
                return;

            auto answer = queue_->submit(std::move(request));

            // Waiting here is right: this is the socket thread, and the
            // message thread is the one doing the work. The interface stays
            // alive throughout, which is the whole point of the queue.
            const auto outcome = answer.get();
            if (outcome.ok())
            {
                Value::Array ids;
                ids.reserve(outcome.commandIds.size());
                for (const auto& id : outcome.commandIds)
                    ids.push_back(Value{id});

                result = Value::object(
                    {{"groupId", Value{outcome.groupId}}, {"commandIds", Value::array(std::move(ids))}});
            }
            else
            {
                failure = errorValue("refused", describe(outcome.code, outcome.message));
            }
        }
    }
    else
    {
        failure = errorValue("unknownMethod", "no method " + method.value());
    }

    if (idValue == nullptr || idValue->isNull())
        return; // a notification: nothing to answer

    Value answer = Value::object({{"jsonrpc", Value{std::string{"2.0"}}}, {"id", *idValue}});
    if (failure.isNull())
        static_cast<void>(answer.set("result", std::move(result)));
    else
        static_cast<void>(answer.set("error", std::move(failure)));

    send(answer);
}

void CopilotBridge::handleAnswer(const Value& message)
{
    // The only thing the DAW asks the copilot is a request typed by the user,
    // so an answer is that request's answer.
    if (const auto* failure = message.find("error"); failure != nullptr && !failure->isNull())
    {
        const auto text = failure->stringAt("message");
        addLine(Line::From::failure, text ? text.value() : std::string{"Le copilote a échoué."});
        setStatus(Status::ready, {});
        return;
    }

    const auto* result = message.find("result");
    if (result == nullptr)
        return;

    const auto text = result->stringAt("text");
    addLine(Line::From::copilot, text ? text.value() : std::string{});

    if (const auto cost = result->find("usage"); cost != nullptr && !cost->isNull())
    {
        juce::Logger::writeToLog("copilot: " + juce::String{domain::json::write(*cost)});
    }

    setStatus(Status::ready, {});
}

void CopilotBridge::send(const Value& message)
{
    const std::lock_guard<std::mutex> lock{writeMutex_};
    if (connection_ == nullptr || !connection_->isConnected())
        return;

    auto text = domain::json::write(message);
    text.push_back('\n');
    static_cast<void>(connection_->write(text.data(), static_cast<int>(text.size())));
}

// ---------------------------------------------------------------------------
// The message thread
// ---------------------------------------------------------------------------

void CopilotBridge::handleAsyncUpdate()
{
    if (queue_ != nullptr)
        static_cast<void>(queue_->drain(wiring_.bus, wiring_.registry));
}

void CopilotBridge::timerCallback()
{
    if (process_ == nullptr)
        return;

    if (process_->isRunning())
        return;

    // The process died. The DAW goes on, and says so: a copilot that vanishes
    // silently is worse than one that is missing.
    const auto exitCode = process_->getExitCode();
    process_.reset();
    stopTimer();

    setStatus(Status::stopped,
              "Le copilote s'est arrêté (code " + std::to_string(exitCode) +
                  "). Le projet est intact, l'édition continue.");
}

domain::Result<Value> CopilotBridge::onMessageThread(std::function<domain::Result<Value>()> job)
{
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        return job();

    auto promise = std::make_shared<std::promise<domain::Result<Value>>>();
    auto future = promise->get_future();

    juce::MessageManager::callAsync([promise, job = std::move(job)]() mutable { promise->set_value(job()); });

    if (future.wait_for(std::chrono::milliseconds{messageThreadTimeoutMs}) != std::future_status::ready)
        return domain::fail(domain::ErrorCode::conflict, "the application did not answer in time");

    return future.get();
}

domain::Result<Value> CopilotBridge::readState() const
{
    return domain::copilot::summarise(wiring_.state, machinePlugins());
}

domain::copilot::MachinePlugins CopilotBridge::machinePlugins() const
{
    domain::copilot::MachinePlugins plugins{};
    if (wiring_.installedPlugins)
        plugins.available = wiring_.installedPlugins();

    return plugins;
}

// ---------------------------------------------------------------------------
// What the panel reads
// ---------------------------------------------------------------------------

CopilotBridge::Status CopilotBridge::status() const
{
    const std::lock_guard<std::mutex> lock{mutex_};
    return status_;
}

std::string CopilotBridge::statusMessage() const
{
    const std::lock_guard<std::mutex> lock{mutex_};
    return statusMessage_;
}

const std::vector<CopilotBridge::Line>& CopilotBridge::transcript() const
{
    // Read from the message thread only, and written under the lock from the
    // socket thread. Returning the vector by reference is safe because it is
    // only ever appended to, and the panel reads it between two repaints.
    const std::lock_guard<std::mutex> lock{mutex_};
    return transcript_;
}

void CopilotBridge::ask(std::string_view request)
{
    if (request.empty())
        return;

    if (!connected_)
    {
        addLine(Line::From::failure,
                "Le copilote n'est pas là. Relancez-le pour lui parler ; le projet ne bouge pas.");
        return;
    }

    addLine(Line::From::user, std::string{request});
    setStatus(Status::working, "Le copilote réfléchit...");

    send(Value::object({{"jsonrpc", Value{std::string{"2.0"}}},
                        {"id", Value{nextRequestId_++}},
                        {"method", Value{std::string{"copilot.ask"}}},
                        {"params", Value::object({{"request", Value{std::string{request}}}})}}));
}

void CopilotBridge::setStatus(Status status, std::string message)
{
    // The log, as well as the panel. A copilot that never appears is diagnosed
    // from the log file, and the panel is gone by the time anyone asks.
    if (!message.empty())
        juce::Logger::writeToLog("copilot: " +
                                 juce::String::fromUTF8(message.data(), static_cast<int>(message.size())));

    {
        const std::lock_guard<std::mutex> lock{mutex_};
        status_ = status;
        statusMessage_ = std::move(message);
    }

    sendChangeMessage();
}

void CopilotBridge::addLine(Line::From from, std::string text)
{
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        transcript_.push_back(Line{from, std::move(text)});
    }

    sendChangeMessage();
}

} // namespace daw::app
