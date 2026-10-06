#pragma once

#include "daw/domain/live/AudioAdvice.h"

#include <juce_events/juce_events.h>

#include <string>
#include <vector>

namespace daw::ui
{

// The sound card, as the « Audio » window is allowed to see it (S24): the
// driver, the output and the buffer, what they really give, and the advice.
// The device manager and the engine stop at the application.
//
// A machine setting, never the project's: none of it is journalled, undone
// or saved with the song.
class AudioHost : public juce::ChangeBroadcaster
{
public:
    struct Choice
    {
        std::string type;
        std::string output;
        int buffer{0};
    };

    // What the window shows as the latency, each part said for what it is.
    struct Latency
    {
        double playSeconds{0.0};          // from the key to the first sample rendered
        bool playMeasured{false};         // a note was played and measured; else the regular wait
        double outputSeconds{0.0};        // what the card declares
        domain::live::BlockTiming blocks; // measured on the audio callback
    };

    AudioHost() = default;
    ~AudioHost() override = default;
    AudioHost(const AudioHost&) = delete;
    AudioHost& operator=(const AudioHost&) = delete;
    AudioHost(AudioHost&&) = delete;
    AudioHost& operator=(AudioHost&&) = delete;

    [[nodiscard]] virtual std::vector<std::string> types() const = 0;
    [[nodiscard]] virtual std::vector<std::string> outputs(const std::string& type) const = 0;
    [[nodiscard]] virtual std::vector<int> buffers(const std::string& type,
                                                   const std::string& output) const = 0;

    [[nodiscard]] virtual Choice current() const = 0;
    [[nodiscard]] virtual double sampleRate() const = 0;
    [[nodiscard]] virtual Latency latency() const = 0;
    [[nodiscard]] virtual domain::live::AudioAdvice advice() const = 0;

    // Opens the card with it; on failure, the old one comes back and said()
    // says why. Broadcasts a change either way.
    virtual void apply(const Choice& choice) = 0;
    [[nodiscard]] virtual std::string said() const = 0;

    // « Tester ma carte »: each setup a few seconds, the sound cut, then the
    // first one back and the advice measured. Refused while the song plays:
    // why, in French, or empty when it may start.
    [[nodiscard]] virtual std::string whyNoTrial() const = 0;
    virtual void startTrial() = 0;
    virtual void cancelTrial() = 0;
    [[nodiscard]] virtual bool trialRunning() const = 0;
    [[nodiscard]] virtual double trialProgress() const = 0; // 0..1
};

} // namespace daw::ui
