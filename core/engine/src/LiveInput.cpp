#include "daw/engine/LiveInput.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{
namespace
{

constexpr int sustainController = 64;
constexpr int allSoundOff = 120;
constexpr int allNotesOff = 123;

} // namespace

const char* LiveInputPlugin::xmlTypeName = "dawLiveInput";
const juce::Identifier LiveInputPlugin::trackProperty{"dawLiveTrack"};

LiveInputPlugin::LiveInputPlugin(tracktion::PluginCreationInfo info)
    : tracktion::Plugin(info)
    , source_(tracktion::createUniqueMPESourceID())
{
}

LiveInputPlugin::~LiveInputPlugin()
{
    notifyListenersOfDeletion();
}

juce::ValueTree LiveInputPlugin::create(const juce::String& track)
{
    juce::ValueTree tree{tracktion::IDs::PLUGIN};
    tree.setProperty(tracktion::IDs::type, xmlTypeName, nullptr);
    tree.setProperty(trackProperty, track, nullptr);
    return tree;
}

juce::String LiveInputPlugin::track() const
{
    return state.getProperty(trackProperty).toString();
}

void LiveInputPlugin::initialise(const tracktion::PluginInitialisationInfo& info)
{
    sampleRate_ = info.sampleRate > 0.0 ? info.sampleRate : 44100.0;
}

void LiveInputPlugin::remember(const domain::live::Event& event) noexcept
{
    const auto status = event.bytes[0] & 0xF0;
    const auto channel = static_cast<std::size_t>(event.bytes[0] & 0x0F);
    const auto data = event.bytes[1] & 0x7F;
    if (status == 0x90 && event.bytes[2] > 0)
    {
        held_[channel].set(static_cast<std::size_t>(data));
        velocity_[channel][static_cast<std::size_t>(data)] = event.bytes[2];
    }
    else if (status == 0x80 || status == 0x90)
        held_[channel].reset(static_cast<std::size_t>(data));
    else if (status == 0xB0 && data == sustainController)
        pedal_.set(channel, event.bytes[2] >= 64);
    else if (status == 0xB0 && (data == allNotesOff || data == allSoundOff))
        held_[channel].reset();
}

void LiveInputPlugin::keepHeldNotesSounding(tracktion::MidiMessageArray& midi) const
{
    bool anyHeld = pedal_.any();
    for (const auto& channel : held_)
        anyHeld = anyHeld || channel.any();
    if (!anyHeld)
        return;

    // Tracktion's cuts for its clips would silence the notes held live too:
    // the flag, read by the instrument as « every voice off », and the
    // messages that name a held pitch, the pedal, or every note.
    midi.isAllNotesOff = false;
    midi.removeIf(
        [this](const tracktion::MidiMessageWithSource& message)
        {
            const auto channel = static_cast<std::size_t>(std::clamp(message.getChannel(), 1, 16) - 1);
            if (message.isNoteOff() || (message.isNoteOn() && message.getVelocity() == 0))
                return held_[channel].test(static_cast<std::size_t>(message.getNoteNumber()));
            if (message.isController())
            {
                const auto number = message.getControllerNumber();
                if (number == sustainController && message.getControllerValue() < 64)
                    return pedal_.test(channel);
                if (number == allNotesOff || number == allSoundOff)
                    return held_[channel].any() || pedal_.test(channel);
            }
            return false;
        });
}

bool LiveInputPlugin::strikeHeldAgain(tracktion::MidiMessageArray& midi, int startSample)
{
    const auto time = static_cast<double>(startSample) / sampleRate_;
    bool struck = false;
    for (std::size_t channel = 0; channel < held_.size(); ++channel)
    {
        if (held_[channel].none())
            continue;
        for (std::size_t note = 0; note < 128; ++note)
        {
            if (held_[channel].test(note))
            {
                midi.addMidiMessage(juce::MidiMessage::noteOn(static_cast<int>(channel) + 1,
                                                              static_cast<int>(note),
                                                              velocity_[channel][note])
                                        .withTimeStamp(time),
                                    source_);
                struck = true;
            }
        }
    }
    return struck;
}

void LiveInputPlugin::applyToBuffer(const tracktion::PluginRenderContext& context)
{
    auto* queue = queue_.load(std::memory_order_acquire);
    auto* midi = context.bufferForMidiMessages;
    const auto samples = context.bufferNumSamples;
    if (queue == nullptr || midi == nullptr || samples <= 0)
        return;

    const auto mode = clock_.load(std::memory_order_relaxed);
    // A render of the live Edit in the application has no clock to place
    // what is played now: the notes wait for the device.
    if (mode == Clock::input && context.isRendering)
        return;

    keepHeldNotesSounding(*midi);

    // Started, stopped, or moved while stopped: every voice was turned off.
    // Stopped, the song's time does not move from one block to the next, so
    // a block that starts elsewhere is a playhead moved. (One moved while it
    // plays cannot be told from the loop going round, which turns nothing
    // off: it is not struck again, and a held note is cut there.)
    const auto editStart = context.editTime.getStart().inSeconds();
    const bool jumped = seen_ && (context.isPlaying != lastPlaying_ ||
                                  (!context.isPlaying && std::abs(editStart - lastEditStart_) > 1.0e-6));
    seen_ = true;
    lastPlaying_ = context.isPlaying;
    lastEditStart_ = editStart;
    bool added = false;
    int last = -1; // the sample of the last message placed: none share one
    if (jumped && strikeHeldAgain(*midi, context.bufferStartSample))
    {
        added = true;
        last = 0; // the attacks struck again have the first sample
    }

    double start = 0.0;
    double delay = 0.0;
    if (mode == Clock::edit)
    {
        start = context.editTime.getStart().inSeconds();
    }
    else
    {
        start = timeline_.begin(domain::live::now(), samples, sampleRate_);
        delay = timeline_.delay();
        wait_.store(delay, std::memory_order_relaxed);
        if (auto* router = router_.load(std::memory_order_acquire); router != nullptr)
            router->publish({start, context.editTime.getStart().inSeconds(), context.isPlaying});
    }

    for (;;)
    {
        if (!hasNext_)
        {
            if (!queue->pop(next_))
                break;
            hasNext_ = true;
        }

        const auto at = std::floor((next_.seconds + delay - start) * sampleRate_);
        if (at >= static_cast<double>(samples))
            break; // a later block's
        auto offset = at <= 0.0 ? 0 : static_cast<int>(at);
        if (offset <= last)
        {
            if (last + 1 >= samples)
                break; // the next block's first
            offset = last + 1;
        }
        last = offset;

        if (next_.size == 3)
        {
            const auto time = static_cast<double>(context.bufferStartSample + offset) / sampleRate_;
            midi->addMidiMessage(juce::MidiMessage(next_.bytes[0], next_.bytes[1], next_.bytes[2], time),
                                 source_);
            if ((next_.bytes[0] & 0xF0) == 0x90 && next_.bytes[2] > 0)
            {
                placedPlayed_.store(next_.seconds, std::memory_order_relaxed);
                placedRendered_.store(start + static_cast<double>(offset) / sampleRate_,
                                      std::memory_order_relaxed);
                placedNote_.store(next_.bytes[1], std::memory_order_relaxed);
            }
            remember(next_);
            added = true;
        }
        hasNext_ = false;
    }

    if (added)
        midi->sortByTimestamp();

    int held = 0;
    for (const auto& channel : held_)
        held += static_cast<int>(channel.count());
    heldNow_.store(held, std::memory_order_relaxed);
}

} // namespace daw::engine
