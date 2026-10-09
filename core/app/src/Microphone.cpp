#include "Microphone.h"

#include "NativeAudio.h"

#include <algorithm>
#include <cmath>

namespace daw::app
{
namespace
{

constexpr double highestRate = 192000.0;
constexpr int injectedBlock = 160; // 10 ms at 16 kHz, as a device would hand them
constexpr int injectedEveryMs = 10;

} // namespace

Microphone::Microphone()
    : buffer_(static_cast<std::size_t>(maxSeconds * highestRate), 0.0f)
{
#if JUCE_WINDOWS
    type_.reset(juce::AudioIODeviceType::createAudioIODeviceType_WASAPI(juce::WASAPIDeviceMode::shared));
#endif
}

Microphone::~Microphone()
{
    discard();
}

std::vector<Microphone::Input> Microphone::inputs() const
{
    std::vector<Input> found;
    if (type_ == nullptr)
        return found;
    type_->scanForDevices();
    const auto endpoints = native::captureEndpoints();
    for (const auto& name : type_->getDeviceNames(true))
    {
        Input input;
        input.name = name;
        for (const auto& endpoint : endpoints)
            if (endpoint.name == name)
            {
                input.bluetooth = endpoint.bluetooth;
                input.isDefault = endpoint.isDefault;
            }
        found.push_back(input);
    }
    return found;
}

juce::String Microphone::defaultFrom(const std::vector<Input>& inputs)
{
    for (const auto& input : inputs)
        if (input.isDefault && !input.bluetooth)
            return input.name;
    for (const auto& input : inputs)
        if (!input.bluetooth)
            return input.name;
    return {};
}

bool Microphone::open(const juce::String& name, juce::String& error)
{
    discard();
    written_.store(0);
    peak_.store(0.0f);

    // A refused microphone is told as one, by the words of the real branch
    // below; an absent one goes through the real branch itself.
    const auto refused = [&name](const juce::String& why)
    {
        return juce::String::fromUTF8("« ") + name + juce::String::fromUTF8(" » ne s'ouvre pas (") + why +
               juce::String::fromUTF8(") — l'accès au micro est-il permis dans les réglages de Windows ?");
    };
    if (failure_ == FailureForTest::refused)
    {
        error = refused(juce::String::fromUTF8("accès refusé, simulé"));
        return false;
    }
    const auto wanted = failure_ == FailureForTest::absent ? juce::String{} : name;

    if (!injected_.empty() && failure_ == FailureForTest::none)
    {
        rate_.store(rate16k);
        injectedAt_ = 0;
        openedName_ = juce::String::fromUTF8("fichier injecté");
        open_.store(true);
        startTimer(injectedEveryMs);
        return true;
    }

    if (type_ == nullptr)
    {
        error = juce::String::fromUTF8("pas de micro sur ce système");
        return false;
    }
    if (wanted.isEmpty())
    {
        error = juce::String::fromUTF8("aucun micro");
        return false;
    }
    type_->scanForDevices();
    device_.reset(type_->createDevice({}, wanted));
    if (device_ == nullptr)
    {
        error = juce::String::fromUTF8("« ") + name + juce::String::fromUTF8(" » n'est plus là");
        return false;
    }
    juce::BigInteger channels;
    channels.setRange(0, std::max(1, device_->getInputChannelNames().size()), true);
    const auto opened =
        device_->open(channels, {}, device_->getCurrentSampleRate(), device_->getDefaultBufferSize());
    if (opened.isNotEmpty())
    {
        // Windows' privacy settings refuse the microphone as an open that
        // fails: said as such, the most likely cause.
        error = refused(opened);
        device_.reset();
        return false;
    }
    openedName_ = name;
    open_.store(true);
    device_->start(this);
    return true;
}

std::vector<float> Microphone::close()
{
    stopTimer();
    if (device_ != nullptr)
    {
        device_->stop();
        device_->close();
        device_.reset();
    }
    open_.store(false);

    const auto count = static_cast<std::size_t>(written_.load());
    const auto rate = rate_.load();
    std::vector<float> out;
    if (count > 0)
    {
        const auto ratio = rate / rate16k;
        out.resize(static_cast<std::size_t>(std::floor(static_cast<double>(count) / ratio)));
        if (std::abs(ratio - 1.0) < 1e-9)
            std::copy(
                buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(out.size()), out.begin());
        else
        {
            juce::WindowedSincInterpolator resampler;
            resampler.process(ratio, buffer_.data(), out.data(), static_cast<int>(out.size()));
        }
    }
    // The voice is kept nowhere.
    std::fill(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(count), 0.0f);
    written_.store(0);
    return out;
}

void Microphone::discard()
{
    static_cast<void>(close());
}

double Microphone::seconds() const noexcept
{
    return static_cast<double>(written_.load()) / rate_.load();
}

float Microphone::takePeakDb() noexcept
{
    const auto peak = peak_.exchange(0.0f);
    return peak > 0.0f ? 20.0f * std::log10(peak) : -100.0f;
}

void Microphone::injectForTest(std::vector<float> samples16k)
{
    injected_ = std::move(samples16k);
}

void Microphone::write(const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!open_.load(std::memory_order_relaxed) || numChannels <= 0 || numSamples <= 0)
        return;
    const auto capacity = static_cast<int>(maxSeconds * rate_.load(std::memory_order_relaxed));
    const auto at = written_.load(std::memory_order_relaxed);
    const auto count = std::min(numSamples, capacity - at);
    float peak = peak_.load(std::memory_order_relaxed);
    for (int index = 0; index < count; ++index)
    {
        float sum = 0.0f;
        for (int channel = 0; channel < numChannels; ++channel)
            if (channels[channel] != nullptr)
                sum += channels[channel][index];
        const auto mono = sum / static_cast<float>(numChannels);
        buffer_[static_cast<std::size_t>(at + index)] = mono;
        peak = std::max(peak, std::abs(mono));
    }
    if (count > 0)
    {
        peak_.store(peak, std::memory_order_relaxed);
        written_.store(at + count, std::memory_order_release);
    }
    everRead_.fetch_add(numSamples, std::memory_order_relaxed);
}

void Microphone::audioDeviceIOCallbackWithContext(const float* const* inputs,
                                                  int numInputs,
                                                  float* const* outputs,
                                                  int numOutputs,
                                                  int numSamples,
                                                  const juce::AudioIODeviceCallbackContext&)
{
    write(inputs, numInputs, numSamples);
    for (int channel = 0; channel < numOutputs; ++channel)
        if (outputs[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputs[channel], numSamples);
}

void Microphone::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    if (device != nullptr && device->getCurrentSampleRate() > 0.0)
        rate_.store(std::min(device->getCurrentSampleRate(), highestRate));
}

void Microphone::audioDeviceStopped() {}

void Microphone::hiResTimerCallback()
{
    // The injected file, 10 ms at a time, the way a device hands its blocks.
    if (injectedAt_ >= injected_.size())
        return;
    const auto count = std::min<std::size_t>(injectedBlock, injected_.size() - injectedAt_);
    const float* channel = injected_.data() + injectedAt_;
    write(&channel, 1, static_cast<int>(count));
    injectedAt_ += count;
}

} // namespace daw::app
