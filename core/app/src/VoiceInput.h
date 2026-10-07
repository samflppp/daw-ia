#pragma once

#include "Microphone.h"
#include "RawKeyboard.h"
#include "VoiceService.h"
#include "daw/domain/Value.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/domain/voice/PushToTalk.h"
#include "daw/ui/model/VoiceHost.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace daw::engine
{
class ProjectProjector;
}

namespace daw::app
{

// The push-to-talk, from the key to the copilot (S25, decided with the
// founder on 7 October 2026).
//
// The right Ctrl (or the button) held: the microphone opens, the song is
// lowered by duckDb, the transcriber starts loading if it is not. Released in
// time: the microphone closes, the song comes back, the samples go to the
// transcriber, and the phrase comes back to the copilot's panel. A sure phrase
// leaves after VoiceHost::sureDelaySeconds unless a key holds it; a doubtful
// one waits for Entrée. Every other end leaves nothing.
//
// Threads: the key arrives on the keyboard's thread (RawKeyboard) and is
// handed to the message thread, where everything here runs; the microphone's
// samples are written by its own device thread (Microphone); the
// transcription runs in another process (VoiceService). Nothing touches the
// engine's audio thread, and nothing is written in the project but what the
// copilot writes, through the bus, in one group.
class VoiceInput final : public ui::VoiceHost,
                         public RawKeyboard::Listener,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    static constexpr float duckDb = -20.0f;

    struct Wiring
    {
        const domain::ProjectState& state;
        engine::ProjectProjector* projector; // the ducking; null in a test
        juce::File settings;                 // this machine's: where the microphone chosen is kept
        juce::File services;                 // the folder holding pyproject.toml
        juce::File replay; // a table of what the model heard (the checks); empty for the model
        // Sends a phrase to the copilot, with what was heard (the context the
        // journal keeps, by digest).
        std::function<void(const std::string& phrase, const domain::Value& spoken)> send;
    };

    explicit VoiceInput(Wiring wiring);
    ~VoiceInput() override;

    VoiceInput(const VoiceInput&) = delete;
    VoiceInput& operator=(const VoiceInput&) = delete;
    VoiceInput(VoiceInput&&) = delete;
    VoiceInput& operator=(VoiceInput&&) = delete;

    // --- RawKeyboard::Listener (the keyboard's thread)
    void keyEvent(int scanCode, bool extended, bool down, bool inFront, double seconds) override;

    // --- ui::VoiceHost
    [[nodiscard]] Stage stage() const override { return stage_; }
    [[nodiscard]] std::string message() const override { return message_; }
    [[nodiscard]] float levelDb() const override { return levelDb_; }
    [[nodiscard]] double elapsed() const override;
    [[nodiscard]] double progress() const override { return service_.installProgress(); }
    [[nodiscard]] double countdown() const override;
    [[nodiscard]] std::vector<Word> words() const override;
    [[nodiscard]] std::vector<std::string> removals() const override { return removals_; }
    void press() override;
    void release() override;
    void holdPhrase() override;
    void sendPhrase(const std::string& text) override;
    void dropPhrase() override;
    void confirm(bool accepted) override;
    void install() override;
    [[nodiscard]] std::vector<ui::VoiceHost::Microphone> microphones() const override;
    [[nodiscard]] std::string microphone() const override;
    void chooseMicrophone(const std::string& name) override;

    // The copilot's commands for the phrase being sent remove things: the
    // person is asked. `answer` is called once, on the message thread.
    void askToConfirm(std::vector<std::string> removals, std::function<void(bool)> answer);

    // --- for the checks
    [[nodiscard]] app::Microphone& microphoneDevice() noexcept { return mic_; }
    [[nodiscard]] VoiceService& service() noexcept { return service_; }
    [[nodiscard]] const std::optional<VoiceService::Heard>& lastHeard() const noexcept { return heard_; }
    // From the key's release to the phrase on the screen, the last time.
    [[nodiscard]] double lastLatencySeconds() const noexcept { return latency_; }
    // A key, by its place, as the keyboard's thread hands it (no keyboard).
    void keyForTest(int scanCode, bool extended, bool down);
    // Whether the window in front is ours; a check forces it.
    std::function<bool()> inFront;

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void apply(const domain::voice::PushToTalk::Step& step, double seconds);
    void heard(bool ok, VoiceService::Heard heard, std::string failure);
    void setStage(Stage stage, std::string message = {});
    void duck(bool on);
    [[nodiscard]] std::vector<std::string> projectNames() const;
    [[nodiscard]] juce::File choiceFile() const;
    [[nodiscard]] juce::String chosenMicrophone() const;

    Wiring wiring_;
    app::Microphone mic_;
    VoiceService service_;
    domain::voice::PushToTalk talk_;

    struct Key
    {
        int scanCode;
        bool extended;
        bool down;
        bool inFront;
        double seconds;
    };
    std::mutex keysMutex_;
    std::vector<Key> keys_;

    Stage stage_{Stage::idle};
    std::string message_;
    float levelDb_{-100.0f};
    double releasedAt_{0.0};
    double shownAt_{0.0};
    double latency_{0.0};
    std::optional<VoiceService::Heard> heard_;
    std::vector<std::string> removals_;
    std::function<void(bool)> confirmAnswer_;
};

} // namespace daw::app
