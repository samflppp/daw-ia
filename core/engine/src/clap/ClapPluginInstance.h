#pragma once

#include <clap/clap.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace daw::engine::clap_host
{

// One loaded .clap shared library, shared by every instance that came out of
// it. The CLAP entry point is initialised once per library and deinitialised
// when the last instance is gone, which is what the specification asks for.
class EntryLibrary
{
public:
    ~EntryLibrary();

    EntryLibrary(const EntryLibrary&) = delete;
    EntryLibrary& operator=(const EntryLibrary&) = delete;

    // Cached by path: a second call for the same file returns the same library.
    [[nodiscard]] static std::shared_ptr<EntryLibrary> open(const juce::File& file);

    [[nodiscard]] const clap_plugin_factory_t* factory() const noexcept { return factory_; }
    [[nodiscard]] const juce::File& file() const noexcept { return file_; }

private:
    EntryLibrary(juce::File file, std::unique_ptr<juce::DynamicLibrary> library);

    juce::File file_;
    std::unique_ptr<juce::DynamicLibrary> library_;
    const clap_plugin_entry_t* entry_{nullptr};
    const clap_plugin_factory_t* factory_{nullptr};
};

class PluginInstance;

// A CLAP parameter seen as a JUCE parameter.
//
// The identity exposed to the rest of the project is the CLAP parameter id, not
// the index: indices move between plugin versions, ids do not. That string is
// what ends up in PluginParam::paramId, therefore in the journal.
class ClapParameter final : public juce::HostedAudioProcessorParameter
{
public:
    ClapParameter(PluginInstance& owner, const clap_param_info_t& info);

    [[nodiscard]] juce::String getParameterID() const override { return juce::String(id_); }
    [[nodiscard]] clap_id clapId() const noexcept { return id_; }

    [[nodiscard]] float getValue() const override;
    void setValue(float newValue) override;
    [[nodiscard]] float getDefaultValue() const override;
    [[nodiscard]] juce::String getName(int maximumStringLength) const override;
    [[nodiscard]] juce::String getLabel() const override { return {}; }
    [[nodiscard]] int getNumSteps() const override;
    [[nodiscard]] bool isDiscrete() const override;
    [[nodiscard]] bool isBoolean() const override;
    [[nodiscard]] bool isAutomatable() const override { return automatable_; }
    [[nodiscard]] juce::String getText(float normalisedValue, int maximumLength) const override;
    [[nodiscard]] float getValueForText(const juce::String& text) const override;

    // Plain value as the plugin sees it, and the normalised form the host uses.
    [[nodiscard]] double toPlain(float normalised) const noexcept;
    [[nodiscard]] float toNormalised(double plain) const noexcept;

    // Records a value the plugin itself reported, without sending it back.
    void storeFromPlugin(double plainValue) noexcept;

private:
    PluginInstance& owner_;
    clap_id id_{};
    juce::String name_;
    double minimum_{0.0};
    double maximum_{1.0};
    double defaultValue_{0.0};
    bool stepped_{false};
    bool automatable_{true};
    std::atomic<float> normalised_{0.0f};
};

// Hosts one CLAP plugin as a juce::AudioPluginInstance, which is the shape
// Tracktion's ExternalPlugin already drives. See ClapPluginFormat.h for what is
// supported and what is deliberately left out.
class PluginInstance final : public juce::AudioPluginInstance, private juce::AsyncUpdater
{
public:
    ~PluginInstance() override;

    // Returns nullptr and fills errorMessage when the plugin refuses to load.
    [[nodiscard]] static std::unique_ptr<PluginInstance> create(const juce::File& file,
                                                                const juce::String& clapId,
                                                                double sampleRate,
                                                                int blockSize,
                                                                juce::String& errorMessage);

    // --- juce::AudioPluginInstance
    void fillInPluginDescription(juce::PluginDescription& description) const override;

    // --- juce::AudioProcessor
    [[nodiscard]] const juce::String getName() const override { return name_; }
    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    [[nodiscard]] double getTailLengthSeconds() const override;
    [[nodiscard]] bool acceptsMidi() const override { return hasNoteInput_; }
    [[nodiscard]] bool producesMidi() const override { return hasNoteOutput_; }
    [[nodiscard]] bool isMidiEffect() const override { return isMidiEffect_; }

    [[nodiscard]] bool hasEditor() const override { return gui_ != nullptr; }
    [[nodiscard]] juce::AudioProcessorEditor* createEditor() override;

    [[nodiscard]] int getNumPrograms() override { return 1; }
    [[nodiscard]] int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    [[nodiscard]] const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- used by ClapParameter
    [[nodiscard]] const clap_plugin_t* plugin() const noexcept { return plugin_; }
    [[nodiscard]] const clap_plugin_params_t* params() const noexcept { return params_; }
    void requestParameterChange(clap_id id, double plainValue);

    // --- used by the editor
    [[nodiscard]] const clap_plugin_gui_t* gui() const noexcept { return gui_; }
    [[nodiscard]] bool isGuiFloating() const noexcept { return guiIsFloating_; }
    [[nodiscard]] static const char* nativeWindowApi();

private:
    explicit PluginInstance(std::shared_ptr<EntryLibrary> library);

    struct OutgoingChange
    {
        enum class Kind : std::uint8_t
        {
            value,
            gestureBegin,
            gestureEnd,
        };

        Kind kind{Kind::value};
        clap_id id{};
        double plainValue{0.0};
    };

    struct PendingChange
    {
        clap_id id{};
        double plainValue{0.0};
    };

    // One CLAP audio port: how many channels it carries, and whether it is the
    // main one — the only port wired to the track's own buffer.
    struct PortLayout
    {
        std::uint32_t channelCount{0};
        bool isMain{false};
    };

    [[nodiscard]] bool loadPlugin(const juce::String& clapId, juce::String& errorMessage);
    void readExtensions();
    void readPorts();
    void buildParameters();
    void refreshAllParameterValues();
    void handleAsyncUpdate() override;

    void pushEvent(const clap_event_header_t& event);
    void pushPending(const PendingChange& change);
    void pushOutgoing(const OutgoingChange& change);
    void flushParameterChange(clap_id id, double plainValue);
    void collectInputEvents(const juce::MidiBuffer& midi, int numSamples);
    void prepareAudioPorts(juce::AudioBuffer<float>& buffer);
    [[nodiscard]] ClapParameter* findParameter(clap_id id) const noexcept;

    // clap_host callbacks. The plugin only ever sees the C struct.
    static const void* hostGetExtension(const clap_host_t* host, const char* extensionId);
    static void hostRequestRestart(const clap_host_t* host);
    static void hostRequestProcess(const clap_host_t* host);
    static void hostRequestCallback(const clap_host_t* host);
    static bool hostIsMainThread(const clap_host_t* host);
    static bool hostIsAudioThread(const clap_host_t* host);
    static void hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags flags);
    static void hostParamsClear(const clap_host_t* host, clap_id paramId, clap_param_clear_flags flags);
    static void hostParamsRequestFlush(const clap_host_t* host);
    static void hostStateMarkDirty(const clap_host_t* host);
    static void hostGuiResizeHintsChanged(const clap_host_t* host);
    static bool hostGuiRequestResize(const clap_host_t* host, std::uint32_t width, std::uint32_t height);
    static bool hostGuiRequestShow(const clap_host_t* host);
    static bool hostGuiRequestHide(const clap_host_t* host);
    static void hostGuiClosed(const clap_host_t* host, bool wasDestroyed);
    static std::uint32_t hostNoteSupportedDialects(const clap_host_t* host);
    static void hostNoteRescan(const clap_host_t* host, std::uint32_t flags);
    static void hostLog(const clap_host_t* host, clap_log_severity severity, const char* message);
    static PluginInstance& from(const clap_host_t* host);

    // Event list callbacks, C side.
    static std::uint32_t inputEventsSize(const clap_input_events_t* list);
    static const clap_event_header_t* inputEventsGet(const clap_input_events_t* list, std::uint32_t index);
    static bool outputEventsTryPush(const clap_output_events_t* list, const clap_event_header_t* event);

    std::shared_ptr<EntryLibrary> library_;
    const clap_plugin_t* plugin_{nullptr};
    clap_host_t host_{};
    clap_host_params_t hostParams_{};
    clap_host_state_t hostState_{};
    clap_host_gui_t hostGui_{};
    clap_host_thread_check_t hostThreadCheck_{};
    clap_host_note_ports_t hostNotePorts_{};
    clap_host_log_t hostLog_{};

    const clap_plugin_params_t* params_{nullptr};
    const clap_plugin_state_t* state_{nullptr};
    const clap_plugin_gui_t* gui_{nullptr};
    const clap_plugin_audio_ports_t* audioPorts_{nullptr};
    const clap_plugin_note_ports_t* notePorts_{nullptr};
    const clap_plugin_tail_t* tail_{nullptr};

    juce::String name_;
    juce::String vendor_;
    juce::String version_;
    juce::String clapId_;
    bool isInstrument_{false};
    bool isMidiEffect_{false};
    bool hasNoteInput_{false};
    bool hasNoteOutput_{false};
    bool notePortTakesMidi_{false};
    bool guiIsFloating_{false};

    std::vector<PortLayout> inputPortLayout_;
    std::vector<PortLayout> outputPortLayout_;
    int mainInputChannels_{0};
    int mainOutputChannels_{2};
    int mainInputPort_{-1};
    int mainOutputPort_{-1};

    std::thread::id mainThread_{std::this_thread::get_id()};
    bool active_{false};
    bool processing_{false};
    std::atomic<bool> insideProcess_{false};

    std::vector<ClapParameter*> parameters_;

    // Input events for one process() call, laid out contiguously so the plugin
    // sees a normal CLAP event list. Both vectors are sized in prepareToPlay
    // and never grow inside process().
    std::vector<std::byte> eventStorage_;
    std::vector<std::size_t> eventOffsets_;
    std::size_t eventBytesUsed_{0};
    clap_input_events_t inputEvents_{};
    clap_output_events_t outputEvents_{};

    // ClapParameter movements waiting to reach the plugin, and plugin-originated
    // changes waiting to reach the host.
    //
    // Both cross the audio thread boundary, so both are single-producer rings
    // with atomic indices: no lock is taken inside process(), and nothing
    // allocates there either. A full ring drops the newest movement rather than
    // block the audio thread — a lost intermediate value of a knob is
    // invisible, a dropout is not.
    static constexpr int ringCapacity = 4096;

    std::array<PendingChange, ringCapacity> pendingRing_{};
    std::atomic<int> pendingWrite_{0};
    std::atomic<int> pendingRead_{0};

    std::array<OutgoingChange, ringCapacity> outgoingRing_{};
    std::atomic<int> outgoingWrite_{0};
    std::atomic<int> outgoingRead_{0};

    // Audio plumbing. Ports the plugin declares but the track does not use are
    // wired to scratch memory: a plugin is allowed to expect every port it
    // announced, and giving it fewer would be a lie about its own layout.
    juce::AudioBuffer<float> scratch_;
    std::vector<clap_audio_buffer_t> inputBuffers_;
    std::vector<clap_audio_buffer_t> outputBuffers_;
    std::vector<float*> inputChannelPointers_;
    std::vector<float*> outputChannelPointers_;
    juce::MidiBuffer producedMidi_;
};

} // namespace daw::engine::clap_host
