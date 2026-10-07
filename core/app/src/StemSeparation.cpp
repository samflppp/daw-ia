#include "StemSeparation.h"

#include <juce_events/juce_events.h>

#include <array>
#include <cmath>
#include <utility>

namespace daw::app
{

namespace
{

constexpr std::array<const char*, 4> stemNames{"vocals", "drums", "bass", "other"};

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

} // namespace

StemSeparation::StemSeparation(juce::File services, juce::File cache)
    : StemSeparation{std::move(services), std::move(cache), {}}
{
}

StemSeparation::StemSeparation(juce::File services, juce::File cache, std::string fakeModel)
    : juce::Thread{"stem separation"}
    , services_{std::move(services)}
    , cache_{std::move(cache)}
    , fake_{std::move(fakeModel)}
    , alive_{std::make_shared<std::atomic<bool>>(true)}
{
}

StemSeparation::~StemSeparation()
{
    alive_->store(false);
    cancel();
    // The process is gone: the thread's read returns, and it ends at once.
    stopThread(4000);
}

bool StemSeparation::running() const noexcept
{
    const auto now = stage_.load();
    return now == Stage::installing || now == Stage::separating;
}

domain::Result<void>
StemSeparation::start(const juce::File& source, const std::string& digest, Model model, Finished finished)
{
    if (running() || isThreadRunning())
        return domain::fail(domain::ErrorCode::conflict, "a separation is already running");
    if (!source.existsAsFile())
        return domain::fail(domain::ErrorCode::notFound,
                            "no such file: " + source.getFullPathName().toStdString());

    source_ = source;
    digest_ = digest;
    model_ = model;
    finished_ = std::move(finished);
    cancelled_.store(false);
    progress_.store(0.0);
    current_ = std::make_shared<std::atomic<bool>>(true);
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        message_.clear();
    }
    stage_.store(Stage::separating);
    startThread();
    return {};
}

void StemSeparation::cancel()
{
    cancelled_.store(true);
    if (current_ != nullptr)
        current_->store(false);
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        if (process_ != nullptr)
            static_cast<void>(process_->kill());
    }
    if (running())
        stage_.store(Stage::cancelled);
}

std::string StemSeparation::status() const
{
    switch (stage_.load())
    {
    case Stage::idle:
        return {};
    case Stage::installing:
        return "Installation du séparateur (la première fois)…";
    case Stage::separating:
        return "Séparation : " + std::to_string(static_cast<int>(std::lround(progress_.load() * 100.0))) +
               " %";
    case Stage::done:
        return "Séparé.";
    case Stage::cancelled:
        return "Séparation annulée.";
    case Stage::failed:
        break;
    }
    const std::lock_guard<std::mutex> lock{mutex_};
    return message_.empty() ? std::string{"La séparation a échoué."} : message_;
}

juce::File StemSeparation::cacheFolder(const std::string& digest, const std::string& signature) const
{
    return cache_.getChildFile(toJuce(digest + "-" + signature));
}

std::optional<std::map<std::string, juce::File>> StemSeparation::cached(const std::string& digest,
                                                                        const std::string& signature) const
{
    const auto folder = cacheFolder(digest, signature);
    std::map<std::string, juce::File> stems;
    for (const auto* name : stemNames)
    {
        const auto file = folder.getChildFile(juce::String{name} + ".wav");
        if (!file.existsAsFile())
            return std::nullopt;
        stems.emplace(name, file);
    }
    return stems;
}

juce::File StemSeparation::python() const
{
    return services_.getChildFile(".venv").getChildFile("Scripts").getChildFile("python.exe");
}

juce::String StemSeparation::modelName() const
{
    if (!fake_.empty())
        return toJuce(fake_);
    return model_ == Model::fast ? "fast" : "best";
}

int StemSeparation::runProcess(const juce::StringArray& command,
                               const std::function<void(const juce::String&)>& line)
{
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        if (cancelled_.load())
            return -1;
        process_ = std::make_unique<juce::ChildProcess>();
        if (!process_->start(command, juce::ChildProcess::wantStdOut))
        {
            process_.reset();
            return -1;
        }
    }

    // One byte at a time: on Windows, juce::ChildProcess::readProcessOutput
    // returns only once the buffer it was given is full or the process is
    // gone, and a progress line is forty bytes. The output is a few lines.
    std::string pending;
    char byte = 0;
    while (process_->readProcessOutput(&byte, 1) == 1)
    {
        if (byte != '\n')
        {
            pending.push_back(byte);
            continue;
        }
        line(juce::String::fromUTF8(pending.c_str()).trimEnd());
        pending.clear();
    }
    if (!pending.empty())
        line(juce::String::fromUTF8(pending.c_str()).trimEnd());

    const std::lock_guard<std::mutex> lock{mutex_};
    const auto code = cancelled_.load() ? -1 : static_cast<int>(process_->getExitCode());
    process_.reset();
    return code;
}

void StemSeparation::run()
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    const auto uv = juce::String{"uv"};
    const auto project = services_.getFullPathName();

    // The services' environment, with the models when a model is asked for.
    // A Python that cannot import them is installed once, and said so.
    const auto real = fake_.empty();
    // Asked without importing: loading PyTorch takes seconds, and a separation
    // that comes from the cache must not pay them.
    const auto check =
        juce::String{"import importlib.util as u, sys; sys.exit(0 if all(u.find_spec(m) for m in "} +
        (real ? "('torch', 'demucs', 'soundfile', 'daw_services')" : "('daw_services',)") + ") else 1)";
    if (!python().existsAsFile() ||
        runProcess({python().getFullPathName(), "-c", check}, [](const auto&) {}) != 0)
    {
        if (cancelled_.load())
            return;
        stage_.store(Stage::installing);
        // --inexact: the voice's extra (S25), when it is there, stays.
        juce::StringArray sync{uv, "sync", "--inexact", "--project", project};
        if (real)
            sync.addArray(juce::StringArray{"--extra", "stems"});
        if (runProcess(sync, [](const auto&) {}) != 0)
        {
            fail("Le séparateur n'a pas pu s'installer (uv sync --extra stems).");
            return;
        }
        stage_.store(Stage::separating);
    }

    // Which weights: the cache's key, asked without loading them.
    std::string signature;
    runProcess(
        {python().getFullPathName(), "-m", "daw_services", "separate", "--model", modelName(), "--signature"},
        [&signature](const juce::String& line)
        {
            if (signature.empty() && line.isNotEmpty())
                signature = line.toStdString();
        });
    if (cancelled_.load())
        return;
    if (signature.empty())
    {
        fail("Le séparateur n'a pas dit son modèle.");
        return;
    }

    if (auto kept = cached(digest_, signature); kept.has_value())
    {
        progress_.store(1.0);
        finish(Separated{std::move(kept).value(),
                         signature,
                         true,
                         (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0});
        return;
    }

    // Written aside, then moved into place whole: a cancelled or failed
    // separation never leaves a cache entry with three stems out of four.
    const auto folder = cacheFolder(digest_, signature);
    const auto aside = cache_.getChildFile(".en-cours-" + juce::Uuid{}.toDashedString());
    std::string failure;
    const auto code =
        runProcess({python().getFullPathName(),
                    "-m",
                    "daw_services",
                    "separate",
                    "--model",
                    modelName(),
                    "--input",
                    source_.getFullPathName(),
                    "--output",
                    aside.getFullPathName()},
                   [this, &failure](const juce::String& line)
                   {
                       const auto event = juce::JSON::parse(line);
                       const auto kind = event.getProperty("event", {}).toString();
                       if (kind == "progress")
                           progress_.store(static_cast<double>(event.getProperty("value", 0.0)));
                       else if (kind == "error")
                           failure = event.getProperty("message", {}).toString().toStdString();
                   });

    if (cancelled_.load())
    {
        static_cast<void>(aside.deleteRecursively());
        return;
    }
    if (code != 0)
    {
        static_cast<void>(aside.deleteRecursively());
        fail(failure.empty() ? std::string{"La séparation a échoué."} : failure);
        return;
    }

    static_cast<void>(folder.deleteRecursively());
    if (!aside.moveFileTo(folder))
    {
        static_cast<void>(aside.deleteRecursively());
        fail("Les stems n'ont pas pu être rangés dans " + folder.getFullPathName().toStdString() + ".");
        return;
    }
    auto stems = cached(digest_, signature);
    if (!stems.has_value())
    {
        fail("Le séparateur n'a pas écrit ses quatre stems.");
        return;
    }
    progress_.store(1.0);
    finish(Separated{std::move(stems).value(),
                     signature,
                     false,
                     (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0});
}

void StemSeparation::fail(const std::string& message)
{
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        message_ = message;
    }
    juce::Logger::writeToLog(toJuce("stems: " + message));
    finish(domain::fail(domain::ErrorCode::storageError, message));
}

void StemSeparation::finish(domain::Result<Separated> result)
{
    if (cancelled_.load())
        return;
    stage_.store(result ? Stage::done : Stage::failed);
    if (result)
        juce::Logger::writeToLog(
            toJuce("stems: " + result.value().signature + ", " +
                   (result.value().fromCache ? std::string{"gardés"} : std::string{"séparés"}) + " en " +
                   std::to_string(result.value().seconds) + " s"));

    juce::MessageManager::callAsync(
        [alive = alive_, current = current_, done = finished_, result = std::move(result)]() mutable
        {
            if (!alive->load() || !current->load() || !done)
                return;
            done(std::move(result));
        });
}

} // namespace daw::app
