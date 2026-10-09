#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
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

// The push-to-talk's transcriber, seen from the application (S25): a Python
// process, `daw-services voix`, kept alive between two phrases so that its
// model stays loaded, and ended after a quarter of an hour without a phrase
// (the model holds 850 MB while it lives).
//
// The DAW listens on 127.0.0.1 and passes the port, as for the copilot; one
// JSON object per line each way. The socket is read by this object's thread;
// every answer reaches the message thread through callAsync, and never after
// this object is gone. A process that dies is said, never fatal.
//
// The first phrase installs what is missing — the `voix` extra (`uv sync
// --inexact`, which leaves the stems' extra in place) and the weights — when
// the person asks for it, and says so; without them the keyboard copilot
// works as before.
class VoiceService final : private juce::Thread, private juce::Timer
{
public:
    enum class Stage
    {
        absent,     // the transcriber is not installed
        installing, // the extra, then the weights
        stopped,    // installed, no process
        starting,   // the process is loading its model
        ready,
        failed
    };

    struct Word
    {
        std::string text;
        double confidence{1.0};
        bool uncertain{false};
        std::string heard; // what was heard, when a homophone was put right
    };
    struct Named
    {
        std::string heard;
        std::string name; // the project's name, or the nearest; empty when none
        bool exact{false};
    };
    struct Heard
    {
        std::string text;
        std::vector<Word> words;
        bool doubtful{true};
        std::vector<std::string> reasons; // French
        std::vector<Named> names;
        double speechSeconds{0.0};
        double seconds{0.0};     // the transcription alone
        double loadSeconds{0.0}; // the model's loading, when this phrase paid it
    };

    using Answered = std::function<void(bool ok, Heard heard, std::string failure)>;

    // `services`: the folder holding pyproject.toml. `replay`, when not empty,
    // is a table of what the model heard (the CI's transcriber).
    VoiceService(juce::File services, juce::File replay);
    ~VoiceService() override;

    VoiceService(const VoiceService&) = delete;
    VoiceService& operator=(const VoiceService&) = delete;
    VoiceService(VoiceService&&) = delete;
    VoiceService& operator=(VoiceService&&) = delete;

    [[nodiscard]] Stage stage() const noexcept { return stage_.load(); }
    [[nodiscard]] std::string status() const; // French, for the screen
    [[nodiscard]] double installProgress() const noexcept { return installProgress_.load(); }

    // Whether the extra and the weights are there, asked of the files, at once.
    [[nodiscard]] bool installed() const;

    // Starts the process if it is not running, so that the model loads while
    // the person speaks. Message thread.
    void warmUp();

    // Sends a phrase (16 kHz, mono). `answered` is called once on the message
    // thread. Message thread.
    void transcribe(const std::vector<float>& samples16k,
                    const std::vector<std::string>& names,
                    Answered answered);

    // Installs what is missing; the stage says how far. Message thread.
    void install();

    // Ends the process (and frees its memory). Message thread.
    void stop();

    // For a check only (S26): the process killed from outside, as a crash
    // of the service would end it. What follows is what a crash gets.
    void killForTest();

    // The process is ended after this long without a phrase.
    static constexpr int idleMinutes = 15;

private:
    void run() override; // the socket thread
    void timerCallback() override;
    void handleLine(const juce::String& line);
    bool send(const std::string& line);
    [[nodiscard]] juce::File python() const;
    void installSteps();
    void setStage(Stage stage, std::string message);

    juce::File services_;
    juce::File replay_;

    std::atomic<Stage> stage_{Stage::stopped};
    std::atomic<double> installProgress_{0.0};
    std::atomic<bool> connected_{false};

    mutable std::mutex mutex_; // guards what follows
    std::string message_;
    std::map<std::int64_t, Answered> waiting_;
    std::vector<std::string> unsent_; // lines asked before the process connected
    std::int64_t nextId_{1};
    double lastUseMs_{0.0};

    std::unique_ptr<juce::StreamingSocket> listener_;
    std::unique_ptr<juce::StreamingSocket> connection_;
    std::unique_ptr<juce::ChildProcess> process_;
    std::uint32_t processId_{0}; // the launcher's, found at start (ProcessTree.h)
    std::unique_ptr<std::thread> installer_;
    std::shared_ptr<std::atomic<bool>> alive_;
};

} // namespace daw::app
