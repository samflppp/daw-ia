#pragma once

#include "daw/domain/mix/Measurement.h"

#include <tracktion_engine/tracktion_engine.h>

namespace daw::engine
{

// The ear of the mix measurement (S20): a plugin that changes nothing and
// hands every block it sees to a domain analyser.
//
// Only ever in the copy of the Edit a MixRender renders, never in the Edit
// that plays: it is offline, so it may take its time, and it is never on the
// audio thread of the session. Tracktion builds it from a tree, so it finds
// its analyser by key in a registry the MixRender fills before the render.
class MixTap final : public tracktion::Plugin
{
public:
    static const char* xmlTypeName;

    // A key enrolled while the returned object lives.
    class Registration
    {
    public:
        explicit Registration(juce::String key);
        ~Registration();
        Registration(Registration&& other) noexcept;
        Registration& operator=(Registration&&) = delete;
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;

    private:
        juce::String key_;
    };

    [[nodiscard]] static Registration enrol(const juce::String& key, domain::mix::StreamAnalyser& analyser);
    [[nodiscard]] static juce::ValueTree create(const juce::String& key);

    explicit MixTap(tracktion::PluginCreationInfo info);
    ~MixTap() override;

    MixTap(const MixTap&) = delete;
    MixTap& operator=(const MixTap&) = delete;
    MixTap(MixTap&&) = delete;
    MixTap& operator=(MixTap&&) = delete;

    juce::String getName() const override { return "DAW IA mix tap"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getShortName(int) override { return "Mix"; }
    juce::String getSelectableDescription() override { return "DAW IA mix tap"; }
    bool canBeDisabled() override { return false; }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    bool canBeAddedToClip() override { return false; }
    bool canBeAddedToRack() override { return false; }
    int getNumOutputChannelsGivenInputs(int numInputChannels) override { return numInputChannels; }
    BusLayout getBusses() const override { return BusLayout::singlePassThrough(); }

    void initialise(const tracktion::PluginInitialisationInfo& info) override;
    void deinitialise() override {}
    void applyToBuffer(const tracktion::PluginRenderContext& context) override;

private:
    domain::mix::StreamAnalyser* analyser_{nullptr};
};

} // namespace daw::engine
