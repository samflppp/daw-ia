#include "NativeAudio.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <audioclient.h>
#include <devicetopology.h>
#include <mmdeviceapi.h>
#include <windows.h>
#endif

namespace daw::app::native
{

#if JUCE_WINDOWS

namespace
{

// PKEY_Device_FriendlyName, defined here rather than linked from a GUID
// library: {a45c254e-df1c-4efd-8020-67d146a850e0}, 14.
const PROPERTYKEY friendlyNameKey{
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// A COM pointer that releases what it holds.
template <typename T>
struct Com
{
    T* p{nullptr};
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com()
    {
        if (p != nullptr)
            p->Release();
    }
    T** put() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// COM for this thread, for the length of one question.
struct ComScope
{
    HRESULT result;
    ComScope()
        : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
    }
    ~ComScope()
    {
        if (SUCCEEDED(result))
            CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

juce::String friendlyName(IMMDevice* device)
{
    Com<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, properties.put())))
        return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    juce::String name;
    if (SUCCEEDED(properties->GetValue(friendlyNameKey, &value)) && value.vt == VT_LPWSTR)
        name = juce::String{value.pwszVal};
    PropVariantClear(&value);
    return name;
}

juce::String identifier(IMMDevice* device)
{
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id)) || id == nullptr)
        return {};
    juce::String text{id};
    CoTaskMemFree(id);
    return text;
}

// The device the endpoint's first connector is plugged into, read from the
// topology: « {2}.\\?\bthhfenum#… » for a hands-free headset.
bool onBluetooth(IMMDevice* device)
{
    Com<IDeviceTopology> topology;
    if (FAILED(device->Activate(
            __uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(topology.put()))))
        return false;
    Com<IConnector> connector;
    if (FAILED(topology->GetConnector(0, connector.put())))
        return false;
    Com<IConnector> other;
    if (FAILED(connector->GetConnectedTo(other.put())))
        return false;
    Com<IPart> part;
    if (FAILED(other->QueryInterface(__uuidof(IPart), reinterpret_cast<void**>(part.put()))))
        return false;
    Com<IDeviceTopology> owner;
    if (FAILED(part->GetTopologyObject(owner.put())))
        return false;
    LPWSTR id = nullptr;
    if (FAILED(owner->GetDeviceId(&id)) || id == nullptr)
        return false;
    const auto text = juce::String{id}.toLowerCase();
    CoTaskMemFree(id);
    return text.contains("bthenum") || text.contains("bthhfenum") || text.contains("bthleenum");
}

juce::String mixFormat(IMMDevice* device)
{
    Com<IAudioClient> client;
    if (FAILED(device->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.put()))))
        return {};
    WAVEFORMATEX* format = nullptr;
    if (FAILED(client->GetMixFormat(&format)) || format == nullptr)
        return {};
    const auto said = juce::String{static_cast<int>(format->nSamplesPerSec)} + " Hz, " +
                      juce::String{static_cast<int>(format->nChannels)} + " canaux";
    CoTaskMemFree(format);
    return said;
}

std::vector<Endpoint> endpoints(EDataFlow flow, DWORD states)
{
    const ComScope com;
    std::vector<Endpoint> found;
    Com<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                nullptr,
                                CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(enumerator.put()))))
        return found;

    juce::String defaultId;
    {
        Com<IMMDevice> standard;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, standard.put())))
            defaultId = identifier(standard.p);
    }

    Com<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, states, collection.put())))
        return found;
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT index = 0; index < count; ++index)
    {
        Com<IMMDevice> device;
        if (FAILED(collection->Item(index, device.put())))
            continue;
        DWORD state = 0;
        device->GetState(&state);
        Endpoint endpoint;
        endpoint.name = friendlyName(device.p);
        endpoint.bluetooth = onBluetooth(device.p);
        endpoint.isDefault = identifier(device.p) == defaultId;
        endpoint.active = state == DEVICE_STATE_ACTIVE;
        if (endpoint.active)
            endpoint.format = mixFormat(device.p);
        found.push_back(endpoint);
    }
    return found;
}

} // namespace

std::vector<Endpoint> captureEndpoints()
{
    return endpoints(eCapture, DEVICE_STATE_ACTIVE);
}

std::optional<Endpoint> renderEndpoint(const juce::String& name)
{
    for (auto& endpoint :
         endpoints(eRender, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED | DEVICE_STATE_DISABLED))
        if (endpoint.name == name)
            return endpoint;
    return std::nullopt;
}

#else

std::vector<Endpoint> captureEndpoints()
{
    return {};
}

std::optional<Endpoint> renderEndpoint(const juce::String&)
{
    return std::nullopt;
}

#endif

} // namespace daw::app::native
