#include "MidiKeyboards.h"

#include "daw/domain/live/Inputs.h"

#include <algorithm>

namespace daw::app
{
namespace
{

// Looked at every second rather than Tracktion's four: unplugged and plugged
// back, a keyboard comes back in a second, like the sound card (S21).
constexpr int scanSeconds = 1;

} // namespace

// One input listened to: its device, and the source of the router it plays
// as. Its callback runs on the MIDI thread.
class MidiKeyboards::Input final : public tracktion::PhysicalMidiInputDevice::Listener
{
public:
    Input(std::shared_ptr<tracktion::MidiInputDevice> device, domain::live::Router& router, int source)
        : device_(std::move(device))
        , physical_(dynamic_cast<tracktion::PhysicalMidiInputDevice*>(device_.get()))
        , router_(router)
        , source_(source)
    {
        if (physical_ != nullptr)
            physical_->addListener(this);
    }

    ~Input() override
    {
        if (physical_ != nullptr)
            physical_->removeListener(this);
    }

    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) = delete;
    Input& operator=(Input&&) = delete;

    void handleIncomingMidiMessage(const juce::MidiMessage& message) override
    {
        const auto at = domain::live::now();
        const auto channel = message.getChannel();
        if (message.isNoteOn())
            static_cast<void>(
                router_.noteOn(source_, channel, message.getNoteNumber(), message.getVelocity(), at));
        else if (message.isNoteOff())
            static_cast<void>(router_.noteOff(source_, channel, message.getNoteNumber(), at));
        else if (message.isController())
            static_cast<void>(router_.controller(
                source_, channel, message.getControllerNumber(), message.getControllerValue(), at));
        else if (message.isPitchWheel())
            static_cast<void>(router_.pitchBend(source_, channel, message.getPitchWheelValue(), at));
    }

    [[nodiscard]] juce::String identifier() const { return device_->getDeviceID(); }
    [[nodiscard]] juce::String name() const { return device_->getName(); }
    [[nodiscard]] int source() const noexcept { return source_; }

private:
    std::shared_ptr<tracktion::MidiInputDevice> device_;
    tracktion::PhysicalMidiInputDevice* physical_{nullptr};
    domain::live::Router& router_;
    int source_{0};
};

MidiKeyboards::MidiKeyboards(tracktion::Engine& engine, domain::live::Router& router)
    : engine_(engine)
    , router_(router)
{
    engine_.getDeviceManager().setMidiDeviceScanIntervalSeconds(scanSeconds);
    engine_.getDeviceManager().addChangeListener(this);
    follow();
}

MidiKeyboards::~MidiKeyboards()
{
    engine_.getDeviceManager().removeChangeListener(this);
    for (const auto& input : inputs_)
        static_cast<void>(router_.release(input->source(), domain::live::now()));
    inputs_.clear();
}

std::vector<std::string> MidiKeyboards::names() const
{
    std::vector<std::string> out;
    for (const auto& input : inputs_)
        out.push_back(input->name().toStdString());
    return out;
}

void MidiKeyboards::changeListenerCallback(juce::ChangeBroadcaster*)
{
    follow();
}

void MidiKeyboards::follow()
{
    // The physical inputs Tracktion has now, by identifier.
    std::vector<std::shared_ptr<tracktion::MidiInputDevice>> present;
    std::vector<std::string> identifiers;
    for (auto& device : engine_.getDeviceManager().getMidiInDevices())
    {
        if (device != nullptr && dynamic_cast<tracktion::PhysicalMidiInputDevice*>(device.get()) != nullptr &&
            device->isEnabled())
        {
            present.push_back(device);
            identifiers.push_back(device->getDeviceID().toStdString());
        }
    }

    std::vector<domain::live::InputSlot> listened;
    for (const auto& input : inputs_)
        listened.push_back({input->identifier().toStdString(), input->source()});
    const auto change = domain::live::follow(
        listened, identifiers, domain::live::Router::firstMidiInput, domain::live::Router::midiInputCount);

    // Gone: what it held is released, and it is said.
    for (const auto& gone : change.removed)
    {
        const auto at = std::find_if(inputs_.begin(),
                                     inputs_.end(),
                                     [&gone](const auto& input) { return input->source() == gone.source; });
        if (at == inputs_.end())
            continue;
        const auto released = router_.release(gone.source, domain::live::now());
        const auto said = "clavier MIDI débranché : " + (*at)->name().toStdString();
        juce::Logger::writeToLog("jeu: " + juce::String::fromUTF8(said.c_str()) + " (" +
                                 juce::String(released) + " notes relâchées)");
        inputs_.erase(at);
        if (onChanged)
            onChanged(said);
    }

    // New: listened to, as the source given.
    for (const auto& added : change.added)
    {
        const auto device =
            std::find_if(present.begin(),
                         present.end(),
                         [&added](const auto& candidate)
                         { return candidate->getDeviceID().toStdString() == added.identifier; });
        if (device == present.end())
            continue;
        inputs_.push_back(std::make_unique<Input>(*device, router_, added.source));
        const auto said = "clavier MIDI branché : " + (*device)->getName().toStdString();
        juce::Logger::writeToLog("jeu: " + juce::String::fromUTF8(said.c_str()));
        if (onChanged)
            onChanged(said);
    }
    for (const auto& waiting : change.waiting)
        juce::Logger::writeToLog("jeu: plus de six claviers MIDI, " + juce::String(waiting) + " attend");
}

} // namespace daw::app
