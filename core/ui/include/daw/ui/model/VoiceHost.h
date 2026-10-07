#pragma once

#include <juce_events/juce_events.h>

#include <string>
#include <vector>

namespace daw::ui
{

// The push-to-talk (S25), as the copilot's panel and the « Audio » window are
// allowed to see it: what the microphone hears, the phrase understood, why it
// is doubtful, and the few gestures around it. The microphone, the key read by
// Raw Input, the transcriber's process and the copilot's socket stop at the
// application.
//
// What leaves, decided with the founder on 7 October 2026: a sure phrase is
// shown, then sent after sureDelaySeconds that any key cancels; a doubtful one
// is shown with its uncertain words marked, and waits for Entrée. A doubtful
// phrase never acts.
class VoiceHost : public juce::ChangeBroadcaster
{
public:
    static constexpr double sureDelaySeconds = 1.5;

    enum class Stage
    {
        absent,       // not installed: the button offers to install it
        installing,   // progress() moves
        idle,         // ready to listen
        opening,      // the key is down, the microphone not yet heard
        listening,    // the microphone is heard: level() and elapsed() move
        transcribing, // released, the transcriber works
        sure,         // shown, leaves after the delay: countdown() moves
        doubtful,     // shown, waits for Entrée
        confirming,   // sent, and the copilot's commands remove: confirm()
        failed        // message() says why
    };

    struct Word
    {
        std::string text;
        bool uncertain{false};
        std::string heard; // what was heard, when a homophone was put right
    };

    VoiceHost() = default;
    ~VoiceHost() override = default;
    VoiceHost(const VoiceHost&) = delete;
    VoiceHost& operator=(const VoiceHost&) = delete;
    VoiceHost(VoiceHost&&) = delete;
    VoiceHost& operator=(VoiceHost&&) = delete;

    [[nodiscard]] virtual Stage stage() const = 0;
    [[nodiscard]] virtual std::string message() const = 0; // French; the reasons of a doubt, a refusal
    [[nodiscard]] virtual float levelDb() const = 0;       // the microphone, last tenth of a second
    [[nodiscard]] virtual double elapsed() const = 0;      // seconds held
    [[nodiscard]] virtual double progress() const = 0;     // the install, 0 to 1
    [[nodiscard]] virtual double countdown() const = 0;    // a sure phrase: 1 down to 0
    [[nodiscard]] virtual std::vector<Word> words() const = 0;
    // What the copilot's commands will remove, when stage() is confirming.
    [[nodiscard]] virtual std::vector<std::string> removals() const = 0;

    // The button on the screen, held with the mouse.
    virtual void press() = 0;
    virtual void release() = 0;

    // A key while a sure phrase counts down: it stays, and waits.
    virtual void holdPhrase() = 0;
    // Entrée on a phrase heard, corrected or not: it goes to the copilot.
    virtual void sendPhrase(const std::string& text) = 0;
    // The phrase heard is dropped, nothing sent.
    virtual void dropPhrase() = 0;
    virtual void confirm(bool accepted) = 0;
    virtual void install() = 0;

    // The microphones of this machine, and the one used: a setting of the
    // machine, never of the project.
    struct Microphone
    {
        std::string name;
        bool bluetooth{false};
    };
    [[nodiscard]] virtual std::vector<Microphone> microphones() const = 0;
    [[nodiscard]] virtual std::string microphone() const = 0;
    virtual void chooseMicrophone(const std::string& name) = 0;
};

} // namespace daw::ui
