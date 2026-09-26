#include "MediaFoundation.h"

#if JUCE_WINDOWS

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

// windows.h first: the Media Foundation headers need its types.
// clang-format off
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
// clang-format on

namespace daw::engine::mediaFoundation
{
namespace
{

// A COM pointer released when it goes out of scope, so an early return on a
// failed call leaks nothing.
struct Release
{
    void operator()(IUnknown* object) const
    {
        if (object != nullptr)
            object->Release();
    }
};

template <typename Interface>
using Com = std::unique_ptr<Interface, Release>;

// COM and Media Foundation, started for as long as one call lasts. The
// message thread has COM already (JUCE starts it); another apartment model
// there is not an error, only a reason not to uninitialise what is not ours.
class Session
{
public:
    Session()
    {
        const auto com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ownsCom_ = com == S_OK || com == S_FALSE;
        started_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    }

    ~Session()
    {
        if (started_)
            MFShutdown();
        if (ownsCom_)
            CoUninitialize();
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    [[nodiscard]] bool started() const noexcept { return started_; }

private:
    bool ownsCom_{false};
    bool started_{false};
};

// A hundred nanoseconds: Media Foundation's unit of time.
constexpr double ticksPerSecond = 10'000'000.0;

constexpr int bytesPerSample = 2;
constexpr int framesPerWrite = 4096;

// The output type the encoder offers nearest to what was asked: same rate,
// same channels, and the bit rate closest to the one chosen.
Com<IMFMediaType> outputType(const GUID& subtype, double sampleRate, int channels, int kilobitsPerSecond)
{
    IMFCollection* raw = nullptr;
    if (FAILED(MFTranscodeGetAudioOutputAvailableTypes(subtype, MFT_ENUM_FLAG_ALL, nullptr, &raw)))
        return {};
    const Com<IMFCollection> types{raw};

    DWORD count = 0;
    types->GetElementCount(&count);

    const auto wantedBytes = static_cast<UINT32>(kilobitsPerSecond) * 1000u / 8u;
    Com<IMFMediaType> best;
    UINT32 bestDistance = UINT32_MAX;

    for (DWORD index = 0; index < count; ++index)
    {
        IUnknown* element = nullptr;
        if (FAILED(types->GetElement(index, &element)))
            continue;
        const Com<IUnknown> unknown{element};

        IMFMediaType* candidate = nullptr;
        if (FAILED(unknown->QueryInterface(IID_PPV_ARGS(&candidate))))
            continue;
        Com<IMFMediaType> type{candidate};

        UINT32 rate = 0;
        UINT32 channelCount = 0;
        UINT32 bytes = 0;
        type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        type->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channelCount);
        type->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes);

        if (rate != static_cast<UINT32>(sampleRate) || channelCount != static_cast<UINT32>(channels))
            continue;

        const auto distance = bytes > wantedBytes ? bytes - wantedBytes : wantedBytes - bytes;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = std::move(type);
        }
    }
    return best;
}

} // namespace

juce::String encode(const juce::AudioBuffer<float>& audio,
                    double sampleRate,
                    const juce::File& target,
                    bool aac,
                    int kilobitsPerSecond)
{
    const Session session;
    if (!session.started())
        return u8"Media Foundation est indisponible sur ce Windows (édition N sans le Media Feature Pack ?)";

    const auto channels = std::clamp(audio.getNumChannels(), 1, 2);
    auto output =
        outputType(aac ? MFAudioFormat_AAC : MFAudioFormat_MP3, sampleRate, channels, kilobitsPerSecond);
    if (output == nullptr)
        return aac ? u8"aucun encodeur AAC à cette fréquence d'échantillonnage"
                   : u8"aucun encodeur MP3 à cette fréquence d'échantillonnage";

    IMFAttributes* rawAttributes = nullptr;
    if (FAILED(MFCreateAttributes(&rawAttributes, 1)))
        return u8"Media Foundation : attributs refusés";
    const Com<IMFAttributes> attributes{rawAttributes};
    attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE,
                        aac ? MFTranscodeContainerType_MPEG4 : MFTranscodeContainerType_MP3);

    static_cast<void>(target.deleteFile());
    IMFSinkWriter* rawWriter = nullptr;
    if (FAILED(MFCreateSinkWriterFromURL(
            target.getFullPathName().toWideCharPointer(), nullptr, attributes.get(), &rawWriter)))
        return juce::String{u8"impossible de créer le fichier "} + target.getFileName();
    const Com<IMFSinkWriter> writer{rawWriter};

    DWORD stream = 0;
    if (FAILED(writer->AddStream(output.get(), &stream)))
        return u8"l'encodeur refuse ce format de sortie";

    IMFMediaType* rawInput = nullptr;
    if (FAILED(MFCreateMediaType(&rawInput)))
        return u8"Media Foundation : type d'entrée refusé";
    const Com<IMFMediaType> input{rawInput};

    const auto blockAlign = static_cast<UINT32>(channels * bytesPerSample);
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bytesPerSample * 8);
    input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, static_cast<UINT32>(sampleRate));
    input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, static_cast<UINT32>(channels));
    input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
    input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, blockAlign * static_cast<UINT32>(sampleRate));
    input->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

    if (FAILED(writer->SetInputMediaType(stream, input.get(), nullptr)))
        return u8"l'encodeur refuse l'audio rendu";
    if (FAILED(writer->BeginWriting()))
        return u8"l'encodeur ne démarre pas";

    // Sixteen-bit PCM in, as the encoders want it. A level past full scale is
    // clipped here: a lossy file has no room above 0 dBFS anyway.
    const auto frames = audio.getNumSamples();
    for (int start = 0; start < frames; start += framesPerWrite)
    {
        const auto count = std::min(framesPerWrite, frames - start);
        const auto bytes = static_cast<DWORD>(count) * blockAlign;

        IMFMediaBuffer* rawBuffer = nullptr;
        if (FAILED(MFCreateMemoryBuffer(bytes, &rawBuffer)))
            return u8"mémoire insuffisante pour encoder";
        const Com<IMFMediaBuffer> buffer{rawBuffer};

        BYTE* data = nullptr;
        buffer->Lock(&data, nullptr, nullptr);
        auto* pcm = reinterpret_cast<std::int16_t*>(data);
        for (int frame = 0; frame < count; ++frame)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto source = std::min(channel, audio.getNumChannels() - 1);
                const auto value = std::clamp(audio.getSample(source, start + frame), -1.0f, 1.0f);
                pcm[frame * channels + channel] = static_cast<std::int16_t>(std::lround(value * 32767.0f));
            }
        }
        buffer->Unlock();
        buffer->SetCurrentLength(bytes);

        IMFSample* rawSample = nullptr;
        if (FAILED(MFCreateSample(&rawSample)))
            return u8"Media Foundation : échantillon refusé";
        const Com<IMFSample> sample{rawSample};
        sample->AddBuffer(buffer.get());
        sample->SetSampleTime(static_cast<LONGLONG>(std::llround(start / sampleRate * ticksPerSecond)));
        sample->SetSampleDuration(static_cast<LONGLONG>(std::llround(count / sampleRate * ticksPerSecond)));

        if (FAILED(writer->WriteSample(stream, sample.get())))
            return u8"l'encodeur s'est arrêté en cours de route";
    }

    if (FAILED(writer->Finalize()))
        return u8"le fichier n'a pas pu être terminé";
    return {};
}

bool decode(const juce::File& file, juce::AudioBuffer<float>& audio, double& sampleRate)
{
    const Session session;
    if (!session.started())
        return false;

    IMFSourceReader* rawReader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(file.getFullPathName().toWideCharPointer(), nullptr, &rawReader)))
        return false;
    const Com<IMFSourceReader> reader{rawReader};

    const auto stream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(stream, TRUE);

    IMFMediaType* rawWanted = nullptr;
    if (FAILED(MFCreateMediaType(&rawWanted)))
        return false;
    const Com<IMFMediaType> wanted{rawWanted};
    wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
    if (FAILED(reader->SetCurrentMediaType(stream, nullptr, wanted.get())))
        return false;

    IMFMediaType* rawActual = nullptr;
    if (FAILED(reader->GetCurrentMediaType(stream, &rawActual)))
        return false;
    const Com<IMFMediaType> actual{rawActual};

    UINT32 rate = 0;
    UINT32 channels = 0;
    actual->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
    actual->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
    if (rate == 0 || channels == 0)
        return false;

    std::vector<float> interleaved;
    for (;;)
    {
        DWORD flags = 0;
        IMFSample* rawSample = nullptr;
        if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, nullptr, &rawSample)))
            return false;
        const Com<IMFSample> sample{rawSample};

        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0 || sample == nullptr)
        {
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
                break;
            continue;
        }

        IMFMediaBuffer* rawBuffer = nullptr;
        if (FAILED(sample->ConvertToContiguousBuffer(&rawBuffer)))
            return false;
        const Com<IMFMediaBuffer> buffer{rawBuffer};

        BYTE* data = nullptr;
        DWORD length = 0;
        buffer->Lock(&data, nullptr, &length);
        const auto* values = reinterpret_cast<const float*>(data);
        interleaved.insert(interleaved.end(), values, values + length / sizeof(float));
        buffer->Unlock();
    }

    const auto frames = static_cast<int>(interleaved.size() / channels);
    audio.setSize(static_cast<int>(channels), frames);
    for (int frame = 0; frame < frames; ++frame)
    {
        for (int channel = 0; channel < static_cast<int>(channels); ++channel)
            audio.setSample(
                channel, frame, interleaved[static_cast<std::size_t>(frame) * channels + channel]);
    }
    sampleRate = static_cast<double>(rate);
    return true;
}

} // namespace daw::engine::mediaFoundation

#else

namespace daw::engine::mediaFoundation
{

juce::String encode(const juce::AudioBuffer<float>&, double, const juce::File&, bool, int)
{
    return u8"l'export MP3 et AAC passe par Media Foundation, qui n'existe que sous Windows";
}

bool decode(const juce::File&, juce::AudioBuffer<float>&, double&)
{
    return false;
}

} // namespace daw::engine::mediaFoundation

#endif
