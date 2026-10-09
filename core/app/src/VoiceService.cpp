#include "VoiceService.h"

#include "ProcessTree.h"
#include "daw/domain/serialization/Json.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>

namespace daw::app
{
namespace
{

using domain::Value;

constexpr int readTimeoutMs = 200;
constexpr int idleCheckMs = 30000;
constexpr int acceptTimeoutMs = 30000;
constexpr const char* modelFolder = "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8";

// Where the service downloads the weights, the same folder as parakeet.py:
// %LOCALAPPDATA% on Windows, the only product target; ~/.cache elsewhere.
juce::File modelsFolder()
{
#if JUCE_WINDOWS
    const auto root = juce::File::getSpecialLocation(juce::File::windowsLocalAppData);
#else
    const auto root = juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".cache");
#endif
    return root.getChildFile("DAW IA").getChildFile("models");
}

// The samples as 16-bit little-endian PCM, in standard base64 (juce::Base64;
// MemoryBlock::toBase64Encoding has an alphabet of its own).
std::string standardBase64(const std::vector<float>& samples)
{
    juce::MemoryOutputStream pcm;
    for (const auto sample : samples)
        pcm.writeShort(static_cast<short>(
            std::clamp(std::lround(static_cast<double>(sample) * 32768.0), -32768L, 32767L)));
    juce::MemoryOutputStream encoded;
    juce::Base64::convertToBase64(encoded, pcm.getData(), pcm.getDataSize());
    return encoded.toString().toStdString();
}

std::string text(const Value& value, std::string_view key, std::string fallback = {})
{
    const auto read = value.stringAt(key);
    return read ? read.value() : fallback;
}

double number(const Value& value, std::string_view key, double fallback = 0.0)
{
    const auto read = value.doubleAt(key);
    return read ? read.value() : fallback;
}

bool flag(const Value& value, std::string_view key, bool fallback)
{
    const auto read = value.boolAt(key);
    return read ? read.value() : fallback;
}

} // namespace

VoiceService::VoiceService(juce::File services, juce::File replay)
    : juce::Thread("DAW IA voix")
    , services_(std::move(services))
    , replay_(std::move(replay))
    , alive_(std::make_shared<std::atomic<bool>>(true))
{
    stage_.store(installed() ? Stage::stopped : Stage::absent);
    startTimer(idleCheckMs);
}

VoiceService::~VoiceService()
{
    alive_->store(false);
    stopTimer();
    stop();
    if (installer_ != nullptr && installer_->joinable())
        installer_->join();
}

juce::File VoiceService::python() const
{
    return services_.getChildFile(".venv").getChildFile("Scripts").getChildFile("python.exe");
}

bool VoiceService::installed() const
{
    if (replay_ != juce::File{})
        return python().existsAsFile();
    const auto weights = modelsFolder().getChildFile(modelFolder);
    for (const auto* name : {"encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt"})
        if (!weights.getChildFile(name).existsAsFile())
            return false;
    return services_.getChildFile(".venv/Lib/site-packages/sherpa_onnx").isDirectory();
}

std::string VoiceService::status() const
{
    const std::lock_guard<std::mutex> lock{mutex_};
    return message_;
}

void VoiceService::setStage(Stage stage, std::string message)
{
    stage_.store(stage);
    const std::lock_guard<std::mutex> lock{mutex_};
    message_ = std::move(message);
}

void VoiceService::killForTest()
{
    processes::killTree(processId_);
}

void VoiceService::warmUp()
{
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        lastUseMs_ = juce::Time::getMillisecondCounterHiRes();
    }
    const auto now = stage_.load();
    if (now == Stage::ready || now == Stage::starting || now == Stage::installing || now == Stage::absent)
        return;

    listener_ = std::make_unique<juce::StreamingSocket>();
    if (!listener_->createListener(0, "127.0.0.1"))
    {
        listener_.reset();
        setStage(Stage::failed, "Aucun port local libre pour la reconnaissance vocale.");
        return;
    }
    juce::StringArray command{python().getFullPathName(),
                              "-m",
                              "daw_services",
                              "voix",
                              "--port",
                              juce::String{listener_->getBoundPort()}};
    if (replay_ != juce::File{})
        command.addArray(juce::StringArray{"--replay", replay_.getFullPathName()});
    process_ = std::make_unique<juce::ChildProcess>();
    juce::Logger::writeToLog("voix: lancement de " + command.joinIntoString(" "));
    const auto before = processes::children();
    const auto started = process_->start(command, 0);
    processId_ = started ? processes::startedBetween(before, processes::children()) : 0;
    if (!started)
    {
        process_.reset();
        listener_.reset();
        setStage(Stage::failed, "La reconnaissance vocale n'a pas démarré.");
        return;
    }
    setStage(Stage::starting, "Chargement de la reconnaissance vocale…");
    startThread();
    // The model loads now, while the person is still speaking.
    send(R"({"id": 0, "method": "load"})");
}

void VoiceService::stop()
{
    signalThreadShouldExit();
    if (connection_ != nullptr)
        connection_->close();
    if (listener_ != nullptr)
        listener_->close();
    stopThread(3000);
    // The launcher, and the Python under it (ProcessTree.h).
    processes::killTree(processId_);
    processId_ = 0;
    if (process_ != nullptr)
    {
        process_->kill();
        process_.reset();
    }
    connection_.reset();
    listener_.reset();
    connected_.store(false);

    std::map<std::int64_t, Answered> unanswered;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        unanswered.swap(waiting_);
        unsent_.clear();
    }
    for (auto& [id, answered] : unanswered)
        juce::MessageManager::callAsync(
            [alive = alive_, answered = std::move(answered)]
            {
                if (alive->load())
                    answered(false, {}, "la reconnaissance vocale s'est arrêtée");
            });
    if (stage_.load() == Stage::ready || stage_.load() == Stage::starting)
        setStage(installed() ? Stage::stopped : Stage::absent, {});
}

void VoiceService::transcribe(const std::vector<float>& samples16k,
                              const std::vector<std::string>& names,
                              Answered answered)
{
    warmUp();
    if (stage_.load() != Stage::ready && stage_.load() != Stage::starting)
    {
        answered(
            false, {}, status().empty() ? std::string{"la reconnaissance vocale n'est pas là"} : status());
        return;
    }
    Value::Array named;
    for (const auto& name : names)
        named.push_back(Value{name});
    std::int64_t id = 0;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        id = nextId_++;
        waiting_[id] = std::move(answered);
    }
    const auto request = Value::object({{"id", Value{id}},
                                        {"method", Value{std::string{"transcribe"}}},
                                        {"samples", Value{standardBase64(samples16k)}},
                                        {"names", Value::array(std::move(named))}});
    send(domain::json::write(request));
}

bool VoiceService::send(const std::string& line)
{
    const std::lock_guard<std::mutex> lock{mutex_};
    if (!connected_.load() || connection_ == nullptr)
    {
        unsent_.push_back(line);
        return true;
    }
    const auto framed = line + "\n";
    return connection_->write(framed.data(), static_cast<int>(framed.size())) ==
           static_cast<int>(framed.size());
}

void VoiceService::run()
{
    if (listener_ == nullptr)
        return;
    if (listener_->waitUntilReady(true, acceptTimeoutMs) <= 0)
    {
        if (!threadShouldExit())
            setStage(Stage::failed, "La reconnaissance vocale ne s'est pas connectée.");
        return;
    }
    auto* accepted = listener_->waitForNextConnection();
    if (accepted == nullptr)
        return;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        connection_.reset(accepted);
        connected_.store(true);
        for (const auto& line : unsent_)
        {
            const auto framed = line + "\n";
            connection_->write(framed.data(), static_cast<int>(framed.size()));
        }
        unsent_.clear();
    }

    std::array<char, 8192> buffer{};
    juce::String incoming;
    while (!threadShouldExit() && connection_->isConnected())
    {
        const auto ready = connection_->waitUntilReady(true, readTimeoutMs);
        if (ready < 0)
            break;
        if (ready == 0)
            continue;
        const auto read = connection_->read(buffer.data(), static_cast<int>(buffer.size()), false);
        if (read <= 0)
            break;
        incoming += juce::String::fromUTF8(buffer.data(), read);
        for (auto newline = incoming.indexOfChar('\n'); newline >= 0; newline = incoming.indexOfChar('\n'))
        {
            const auto line = incoming.substring(0, newline).trim();
            incoming = incoming.substring(newline + 1);
            if (line.isNotEmpty())
                handleLine(line);
        }
    }
    connected_.store(false);
    if (!threadShouldExit())
    {
        juce::Logger::writeToLog(juce::String::fromUTF8("voix: le processus s'est arrêté"));
        setStage(installed() ? Stage::stopped : Stage::absent, "La reconnaissance vocale s'est arrêtée.");
    }
}

void VoiceService::handleLine(const juce::String& line)
{
    auto parsed = domain::json::read(line.toStdString());
    if (!parsed)
        return;
    const auto& message = parsed.value();
    const auto event = text(message, "event", "");
    const auto readId = message.intAt("id");
    const auto id = readId ? readId.value() : std::int64_t{-1};

    if (event == "loaded")
    {
        setStage(Stage::ready, {});
        juce::Logger::writeToLog(juce::String::fromUTF8("voix: modèle chargé en ") +
                                 juce::String{number(message, "seconds", 0.0), 2} + " s");
        return;
    }
    if (event == "error" && text(message, "code", "") == "absent")
        setStage(Stage::absent, text(message, "message", ""));

    Answered answered;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        if (auto found = waiting_.find(id); found != waiting_.end())
        {
            answered = std::move(found->second);
            waiting_.erase(found);
        }
    }
    if (!answered)
        return;

    if (event != "heard")
    {
        const auto failure = text(message, "message", "la transcription a échoué");
        juce::MessageManager::callAsync(
            [alive = alive_, answered = std::move(answered), failure]
            {
                if (alive->load())
                    answered(false, {}, failure);
            });
        return;
    }

    Heard heard;
    heard.text = text(message, "text", "");
    heard.doubtful = flag(message, "doubtful", true);
    heard.speechSeconds = number(message, "speech", 0.0);
    heard.seconds = number(message, "seconds", 0.0);
    heard.loadSeconds = number(message, "loaded", 0.0);
    if (const auto* words = message.find("words"); words != nullptr && words->isArray())
        for (const auto& word : *words->asArray())
            heard.words.push_back(Word{text(word, "text", ""),
                                       number(word, "confidence", 0.0),
                                       flag(word, "uncertain", false),
                                       text(word, "heard", "")});
    if (const auto* reasons = message.find("reasons"); reasons != nullptr && reasons->isArray())
        for (const auto& reason : *reasons->asArray())
            if (const auto said = reason.asString(); said)
                heard.reasons.push_back(said.value());
    if (const auto* names = message.find("names"); names != nullptr && names->isArray())
        for (const auto& named : *names->asArray())
            heard.names.push_back(
                Named{text(named, "heard", ""), text(named, "name", ""), flag(named, "exact", false)});
    juce::MessageManager::callAsync(
        [alive = alive_, answered = std::move(answered), heard = std::move(heard)]() mutable
        {
            if (alive->load())
                answered(true, std::move(heard), {});
        });
}

void VoiceService::timerCallback()
{
    double last = 0.0;
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        last = lastUseMs_;
    }
    if ((stage_.load() == Stage::ready) &&
        juce::Time::getMillisecondCounterHiRes() - last > idleMinutes * 60000.0)
    {
        juce::Logger::writeToLog(
            juce::String::fromUTF8("voix: un quart d'heure sans phrase, le modèle est libéré"));
        stop();
    }
}

void VoiceService::install()
{
    if (stage_.load() == Stage::installing)
        return;
    if (installer_ != nullptr && installer_->joinable())
        installer_->join();
    setStage(Stage::installing, "Installation de la reconnaissance vocale…");
    installProgress_.store(0.0);
    installer_ = std::make_unique<std::thread>([this] { installSteps(); });
}

void VoiceService::installSteps()
{
    const auto runProcess =
        [this](const juce::StringArray& command, const std::function<void(const juce::String&)>& line)
    {
        juce::ChildProcess process;
        if (!process.start(command, juce::ChildProcess::wantStdOut))
            return -1;
        std::string pending;
        char byte = 0;
        while (alive_->load() && process.readProcessOutput(&byte, 1) == 1)
        {
            if (byte != '\n')
            {
                pending.push_back(byte);
                continue;
            }
            line(juce::String::fromUTF8(pending.c_str()).trimEnd());
            pending.clear();
        }
        return static_cast<int>(process.getExitCode());
    };

    // --inexact: the stems' extra, when it is there, stays.
    if (runProcess({"uv", "sync", "--inexact", "--project", services_.getFullPathName(), "--extra", "voix"},
                   [](const juce::String&) {}) != 0)
    {
        setStage(Stage::failed, "La reconnaissance vocale n'a pas pu s'installer (uv sync --extra voix).");
        return;
    }
    installProgress_.store(0.05);
    std::string failure;
    const auto code = runProcess(
        {python().getFullPathName(), "-m", "daw_services", "voix", "--install"},
        [this, &failure](const juce::String& line)
        {
            const auto event = juce::JSON::parse(line);
            const auto kind = event.getProperty("event", {}).toString();
            if (kind == "progress")
                installProgress_.store(0.05 + 0.95 * static_cast<double>(event.getProperty("value", 0.0)));
            else if (kind == "error")
                failure = event.getProperty("message", {}).toString().toStdString();
        });
    if (code != 0 || !installed())
    {
        setStage(Stage::failed,
                 failure.empty() ? std::string{"Le téléchargement du modèle a échoué."} : failure);
        return;
    }
    installProgress_.store(1.0);
    setStage(Stage::stopped, {});
    juce::Logger::writeToLog(juce::String::fromUTF8("voix: reconnaissance vocale installée"));
}

} // namespace daw::app
