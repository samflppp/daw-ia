#pragma once

#include "daw/domain/Result.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace daw::app
{

// One separation of an audio file into its four stems (S22), off the message
// thread and off the audio thread.
//
// The work is done by another process, `daw-services separate`, run with the
// Python of the services' own environment; this class reads its progress line
// by line on a thread of its own. Cancelling ends that process: cancel()
// returns at once, whatever the model is doing, and the application never
// waits for a computation to finish (the lesson of the S20 mix).
//
// The first separation installs the models' environment (`uv sync --extra
// stems`, PyTorch for the CPU, a few hundred megabytes) and downloads the
// weights: it says so, and it is cancellable like the rest.
//
// A separation is kept, under the source's content digest and the model's
// signature: separating the same file with the same model again is
// immediate, and a new version of the weights is a new signature.
class StemSeparation final : private juce::Thread
{
public:
    enum class Model
    {
        best,
        fast
    };

    enum class Stage
    {
        idle,
        installing, // the models' environment, the first time
        separating,
        done,
        failed,
        cancelled
    };

    struct Separated
    {
        std::map<std::string, juce::File> stems; // "vocals", "drums", "bass", "other"
        std::string signature;                   // "htdemucs-955717e8"
        bool fromCache{false};
        double seconds{0.0};
    };

    using Finished = std::function<void(domain::Result<Separated>)>;

    // `services`: the folder holding pyproject.toml. `cache`: where the stems
    // are kept between separations (%LOCALAPPDATA%\DAW IA\stems). The third
    // constructor runs another separator than the one a Model names — "fake",
    // the band filters of the CI —, whatever start() is asked for.
    StemSeparation(juce::File services, juce::File cache);
    StemSeparation(juce::File services, juce::File cache, std::string fakeModel);
    ~StemSeparation() override;

    StemSeparation(const StemSeparation&) = delete;
    StemSeparation& operator=(const StemSeparation&) = delete;
    StemSeparation(StemSeparation&&) = delete;
    StemSeparation& operator=(StemSeparation&&) = delete;

    // Starts separating `source`, whose content digest is `digest`. Returns
    // at once. `finished` is called once on the message thread — never after
    // cancel(), never after this object is gone. A start while running is
    // refused.
    [[nodiscard]] domain::Result<void>
    start(const juce::File& source, const std::string& digest, Model model, Finished finished);

    // Ends the separation in flight. Returns at once.
    void cancel();

    [[nodiscard]] Stage stage() const noexcept { return stage_.load(); }
    [[nodiscard]] double progress() const noexcept { return progress_.load(); } // 0 to 1
    [[nodiscard]] bool running() const noexcept;

    // One sentence for the screen: "Séparation : 42 %", "Installation du
    // séparateur…", or what failed.
    [[nodiscard]] std::string status() const;

    // The stems' folder for this source and model, filled or not.
    [[nodiscard]] juce::File cacheFolder(const std::string& digest, const std::string& signature) const;

    // The four stems kept for this source and signature, if they all are.
    [[nodiscard]] std::optional<std::map<std::string, juce::File>> cached(const std::string& digest,
                                                                          const std::string& signature) const;

private:
    void run() override;

    // Runs `command`, hands every line of its output to `line`, and returns
    // its exit code; -1 when it could not start or was cancelled.
    int runProcess(const juce::StringArray& command, const std::function<void(const juce::String&)>& line);

    [[nodiscard]] juce::File python() const;
    [[nodiscard]] juce::String modelName() const;
    void finish(domain::Result<Separated> result);
    void fail(const std::string& message);

    juce::File services_;
    juce::File cache_;
    std::string fake_;

    // What the thread works on, set by start() before it runs.
    juce::File source_;
    std::string digest_;
    Model model_{Model::best};
    Finished finished_;

    std::atomic<Stage> stage_{Stage::idle};
    std::atomic<double> progress_{0.0};
    std::atomic<bool> cancelled_{false};

    mutable std::mutex mutex_; // guards process_ and message_
    std::unique_ptr<juce::ChildProcess> process_;
    std::string message_;

    // Shared with the callbacks posted to the message thread: false once
    // this object is gone or the separation cancelled.
    std::shared_ptr<std::atomic<bool>> alive_;
    // The same, for the separation in flight: cancel() turns it off.
    std::shared_ptr<std::atomic<bool>> current_;
};

} // namespace daw::app
