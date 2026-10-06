#pragma once

#include "daw/engine/AudioSettings.h"
#include "daw/ui/model/AudioHost.h"

#include <tracktion_engine/tracktion_engine.h>

#include <functional>

namespace daw::app
{

// The application's answer to AudioHost (S24): the engine's AudioSettings
// for the card, and the live inputs of the Edit for the latency of the keys.
//
// The latency shown is the one --verify-jeu measures: from the key to the
// first sample rendered, read on the live input — the last note played
// since the card last changed, or, before one, the regular wait every note
// gets (block and margin), which the verification proves to the
// millisecond — plus what the card declares.
class SoundCard final : public ui::AudioHost, private juce::Timer
{
public:
    SoundCard(engine::AudioSettings& settings, tracktion::Edit& edit, std::function<bool()> songPlaying);
    ~SoundCard() override;

    SoundCard(const SoundCard&) = delete;
    SoundCard& operator=(const SoundCard&) = delete;
    SoundCard(SoundCard&&) = delete;
    SoundCard& operator=(SoundCard&&) = delete;

    [[nodiscard]] std::vector<std::string> types() const override;
    [[nodiscard]] std::vector<std::string> outputs(const std::string& type) const override;
    [[nodiscard]] std::vector<int> buffers(const std::string& type, const std::string& output) const override;
    [[nodiscard]] Choice current() const override;
    [[nodiscard]] double sampleRate() const override { return settings_.sampleRate(); }
    [[nodiscard]] Latency latency() const override;
    [[nodiscard]] domain::live::AudioAdvice advice() const override { return settings_.advice(); }
    void apply(const Choice& choice) override;
    [[nodiscard]] std::string said() const override { return settings_.said().toStdString(); }
    [[nodiscard]] std::string whyNoTrial() const override;
    void startTrial() override;
    void cancelTrial() override { settings_.cancelTrial(); }
    [[nodiscard]] bool trialRunning() const override { return settings_.trialRunning(); }
    [[nodiscard]] double trialProgress() const override { return settings_.trialProgress(); }

private:
    // A card changed under the window — lost, given back by the keeper —
    // is told to it like a choice made in it.
    void timerCallback() override;

    engine::AudioSettings& settings_;
    tracktion::Edit& edit_;
    std::function<bool()> songPlaying_;
    engine::AudioSettings::Choice shown_;

    // When the card last changed, on the input clock: a note played before
    // it was measured with the old buffer.
    double changedAt_{0.0};
};

} // namespace daw::app
