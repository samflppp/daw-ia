#pragma once

#include "daw/ui/PanelRegistry.h"
#include "daw/ui/model/AudioHost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace daw::ui
{

// The « Audio » window (S24): the sound card's driver, output and buffer,
// the frequency, the latency — measured where it can be, declared where it
// cannot, each said for what it is — and the advice, with a button to take it.
//
// A machine setting: what the person chooses here goes to the AudioHost,
// never to the bus.
class AudioPanel final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit AudioPanel(const PanelContext& context);
    ~AudioPanel() override;

    AudioPanel(const AudioPanel&) = delete;
    AudioPanel& operator=(const AudioPanel&) = delete;
    AudioPanel(AudioPanel&&) = delete;
    AudioPanel& operator=(AudioPanel&&) = delete;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // What the window shows, as text, for the verification.
    [[nodiscard]] juce::String latencyLine() const { return latency_.getText(); }
    [[nodiscard]] juce::String blocksLine() const { return blocks_.getText(); }
    [[nodiscard]] juce::String adviceLine() const { return advice_.getText(); }
    [[nodiscard]] juce::String saidLine() const { return said_.getText(); }
    [[nodiscard]] juce::String bufferShown() const { return buffer_.getText(); }

    // Chooses the way the person does, through the window's own menus.
    void chooseType(const std::string& type);
    void chooseBuffer(int samples);
    void takeAdvice();
    void startTrial();
    [[nodiscard]] bool trialRunning() const { return audio_.trialRunning(); }

    // The push-to-talk's microphone (S25), a setting of this machine.
    void chooseMicrophone(const std::string& name);
    [[nodiscard]] juce::String microphoneShown() const { return microphone_.getText(); }
    [[nodiscard]] juce::String microphoneLine() const { return microphoneSaid_.getText(); }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    void refresh();
    void refreshMeasures();
    void applyFrom(const std::string& type, const std::string& output, int buffer);
    void refreshMicrophones();

    const Tokens& tokens_;
    DawLookAndFeel& lookAndFeel_;
    AudioHost& audio_;
    VoiceHost& voice_;
    bool titled_{false};

    std::vector<std::string> types_;
    std::vector<std::string> outputs_;
    std::vector<int> buffers_;

    juce::Label typeLabel_;
    juce::ComboBox type_;
    juce::Label outputLabel_;
    juce::ComboBox output_;
    juce::Label bufferLabel_;
    juce::ComboBox buffer_;
    juce::Label rate_;
    juce::Label latency_;
    juce::Label blocks_;
    juce::Label advice_;
    juce::TextButton takeAdvice_;
    juce::TextButton trial_;
    juce::Label said_;
    std::vector<VoiceHost::Microphone> microphones_;
    juce::Label microphoneLabel_;
    juce::ComboBox microphone_;
    juce::Label microphoneSaid_;
};

} // namespace daw::ui
