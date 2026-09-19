// A real CLAP plugin, built by this repository, used to prove the CLAP host.
//
// It exists because a host cannot be tested against a mock: the whole question
// of the week is whether a real shared library, loaded through the real CLAP
// entry point, driven through real CLAP events, produces sound in this project.
// A third-party plugin proves that too, but it cannot be committed, it is not
// installed on every machine, and a commercial instrument is often silent until
// its own content is loaded — which would make a failing test say nothing.
//
// So this plugin is deliberately dull and deliberately audible: a sine at the
// pitch of the last note, one gain parameter, and an opaque state that holds
// that gain. It also reports its own parameter change with a gesture around it
// when it receives MIDI controller 1, which is how the parameter bridge is
// tested from the plugin's side.

#include <clap/clap.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{

constexpr char pluginId[] = "daw.test.sine";
constexpr clap_id gainParamId = 0;
constexpr std::uint32_t stateMagic = 0x44415731; // "DAW1"

// The CRT copy functions are deprecated on MSVC and the project keeps -Werror,
// so the two strings this plugin writes are copied by hand.
void copyText(char* destination, std::size_t size, const char* text)
{
    if (destination == nullptr || size == 0)
        return;

    const auto length = std::min(std::strlen(text), size - 1);
    std::memcpy(destination, text, length);
    destination[length] = '\0';
}

const char* const features[] = {
    CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER, CLAP_PLUGIN_FEATURE_STEREO, nullptr};

const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT,
    pluginId,
    "DAW Test Sine",
    "DAW IA",
    "",
    "",
    "",
    "0.1",
    "A sine, so that a test can hear something",
    features,
};

struct Plugin
{
    clap_plugin_t plugin{};
    const clap_host_t* host{nullptr};

    double sampleRate{44100.0};
    double gain{0.5};

    // One voice is enough: the point is a signal, not polyphony.
    bool noteOn{false};
    int key{-1};
    double phase{0.0};

    void handleNoteOn(int noteKey)
    {
        key = noteKey;
        noteOn = true;
    }

    void handleNoteOff(int noteKey)
    {
        if (noteKey < 0 || noteKey == key)
            noteOn = false;
    }

    void reportGainToHost(const clap_output_events_t* out, double value, std::uint32_t time)
    {
        if (out == nullptr)
            return;

        clap_event_param_gesture_t begin{};
        begin.header.size = static_cast<std::uint32_t>(sizeof(begin));
        begin.header.time = time;
        begin.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        begin.header.type = static_cast<std::uint16_t>(CLAP_EVENT_PARAM_GESTURE_BEGIN);
        begin.param_id = gainParamId;
        out->try_push(out, &begin.header);

        clap_event_param_value_t change{};
        change.header.size = static_cast<std::uint32_t>(sizeof(change));
        change.header.time = time;
        change.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        change.header.type = static_cast<std::uint16_t>(CLAP_EVENT_PARAM_VALUE);
        change.param_id = gainParamId;
        change.note_id = -1;
        change.port_index = -1;
        change.channel = -1;
        change.key = -1;
        change.value = value;
        out->try_push(out, &change.header);

        clap_event_param_gesture_t end{};
        end.header.size = static_cast<std::uint32_t>(sizeof(end));
        end.header.time = time;
        end.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        end.header.type = static_cast<std::uint16_t>(CLAP_EVENT_PARAM_GESTURE_END);
        end.param_id = gainParamId;
        out->try_push(out, &end.header);
    }

    void handleEvent(const clap_event_header_t& header, const clap_output_events_t* out)
    {
        if (header.space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;

        switch (header.type)
        {
        case CLAP_EVENT_NOTE_ON:
            handleNoteOn(reinterpret_cast<const clap_event_note_t&>(header).key);
            break;

        case CLAP_EVENT_NOTE_OFF:
        case CLAP_EVENT_NOTE_CHOKE:
            handleNoteOff(reinterpret_cast<const clap_event_note_t&>(header).key);
            break;

        case CLAP_EVENT_PARAM_VALUE:
        {
            const auto& value = reinterpret_cast<const clap_event_param_value_t&>(header);
            if (value.param_id == gainParamId)
                gain = std::clamp(value.value, 0.0, 1.0);
            break;
        }

        case CLAP_EVENT_MIDI:
        {
            const auto& midi = reinterpret_cast<const clap_event_midi_t&>(header);
            const auto status = static_cast<std::uint8_t>(midi.data[0] & 0xf0);

            if (status == 0x90 && midi.data[2] > 0)
                handleNoteOn(static_cast<int>(midi.data[1]));
            else if (status == 0x80 || (status == 0x90 && midi.data[2] == 0))
                handleNoteOff(static_cast<int>(midi.data[1]));
            else if (status == 0xb0 && midi.data[1] == 1)
            {
                // The plugin moves its own parameter, and says so with a
                // gesture around it: exactly what happens when a user turns
                // a knob in a plugin's own window.
                gain = static_cast<double>(midi.data[2]) / 127.0;
                reportGainToHost(out, gain, header.time);
            }
            break;
        }

        default:
            break;
        }
    }
};

Plugin& from(const clap_plugin_t* plugin)
{
    return *static_cast<Plugin*>(plugin->plugin_data);
}

// --- audio ports -----------------------------------------------------------

std::uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 0u : 1u;
}

bool audioPortsGet(const clap_plugin_t*, std::uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (isInput || index != 0 || info == nullptr)
        return false;

    info->id = 0;
    copyText(info->name, sizeof(info->name), "Out");
    info->flags = static_cast<std::uint32_t>(CLAP_AUDIO_PORT_IS_MAIN);
    info->channel_count = 2u;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPorts = {audioPortsCount, audioPortsGet};

// --- note ports ------------------------------------------------------------

std::uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1u : 0u;
}

bool notePortsGet(const clap_plugin_t*, std::uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0 || info == nullptr)
        return false;

    info->id = 0;
    info->supported_dialects = static_cast<std::uint32_t>(CLAP_NOTE_DIALECT_CLAP) |
                               static_cast<std::uint32_t>(CLAP_NOTE_DIALECT_MIDI);
    info->preferred_dialect = static_cast<std::uint32_t>(CLAP_NOTE_DIALECT_MIDI);
    copyText(info->name, sizeof(info->name), "Notes");
    return true;
}

const clap_plugin_note_ports_t notePorts = {notePortsCount, notePortsGet};

// --- parameters ------------------------------------------------------------

std::uint32_t paramsCount(const clap_plugin_t*)
{
    return 1;
}

bool paramsGetInfo(const clap_plugin_t*, std::uint32_t index, clap_param_info_t* info)
{
    if (index != 0 || info == nullptr)
        return false;

    std::memset(info, 0, sizeof(*info));
    info->id = gainParamId;
    info->flags = static_cast<std::uint32_t>(CLAP_PARAM_IS_AUTOMATABLE);
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = 0.5;
    copyText(info->name, sizeof(info->name), "Gain");
    return true;
}

bool paramsGetValue(const clap_plugin_t* plugin, clap_id id, double* out)
{
    if (id != gainParamId || out == nullptr)
        return false;

    *out = from(plugin).gain;
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id id, double value, char* out, std::uint32_t size)
{
    if (id != gainParamId || out == nullptr || size == 0)
        return false;

    std::snprintf(out, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id id, const char* text, double* out)
{
    if (id != gainParamId || text == nullptr || out == nullptr)
        return false;

    *out = std::clamp(std::atof(text), 0.0, 1.0);
    return true;
}

void paramsFlush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
{
    if (in == nullptr)
        return;

    auto& self = from(plugin);
    const auto count = in->size(in);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        if (const auto* header = in->get(in, index); header != nullptr)
            self.handleEvent(*header, out);
    }
}

const clap_plugin_params_t params = {
    paramsCount, paramsGetInfo, paramsGetValue, paramsValueToText, paramsTextToValue, paramsFlush};

// --- state -----------------------------------------------------------------

bool stateSave(const clap_plugin_t* plugin, const clap_ostream_t* stream)
{
    if (stream == nullptr)
        return false;

    const auto& self = from(plugin);

    const std::uint32_t magic = stateMagic;
    if (stream->write(stream, &magic, sizeof(magic)) != static_cast<std::int64_t>(sizeof(magic)))
        return false;

    const double gain = self.gain;
    return stream->write(stream, &gain, sizeof(gain)) == static_cast<std::int64_t>(sizeof(gain));
}

bool stateLoad(const clap_plugin_t* plugin, const clap_istream_t* stream)
{
    if (stream == nullptr)
        return false;

    std::uint32_t magic = 0;
    if (stream->read(stream, &magic, sizeof(magic)) != static_cast<std::int64_t>(sizeof(magic)) ||
        magic != stateMagic)
        return false;

    double gain = 0.5;
    if (stream->read(stream, &gain, sizeof(gain)) != static_cast<std::int64_t>(sizeof(gain)))
        return false;

    from(plugin).gain = std::clamp(gain, 0.0, 1.0);
    return true;
}

const clap_plugin_state_t state = {stateSave, stateLoad};

// --- plugin ----------------------------------------------------------------

bool pluginInit(const clap_plugin_t*)
{
    return true;
}

void pluginDestroy(const clap_plugin_t* plugin)
{
    delete &from(plugin);
}

bool pluginActivate(const clap_plugin_t* plugin, double sampleRate, std::uint32_t, std::uint32_t)
{
    auto& self = from(plugin);
    self.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    self.phase = 0.0;
    return true;
}

void pluginDeactivate(const clap_plugin_t*) {}

bool pluginStartProcessing(const clap_plugin_t*)
{
    return true;
}

void pluginStopProcessing(const clap_plugin_t*) {}

void pluginReset(const clap_plugin_t* plugin)
{
    auto& self = from(plugin);
    self.noteOn = false;
    self.phase = 0.0;
}

clap_process_status pluginProcess(const clap_plugin_t* plugin, const clap_process_t* process)
{
    if (process == nullptr)
        return CLAP_PROCESS_ERROR;

    auto& self = from(plugin);

    if (process->in_events != nullptr)
    {
        const auto count = process->in_events->size(process->in_events);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (const auto* header = process->in_events->get(process->in_events, index); header != nullptr)
                self.handleEvent(*header, process->out_events);
        }
    }

    if (process->audio_outputs_count == 0 || process->audio_outputs[0].data32 == nullptr)
        return CLAP_PROCESS_CONTINUE;

    auto& output = process->audio_outputs[0];
    const auto frames = process->frames_count;

    const double frequency =
        self.key >= 0 ? 440.0 * std::pow(2.0, static_cast<double>(self.key - 69) / 12.0) : 0.0;
    const double increment = frequency * 2.0 * 3.14159265358979323846 / self.sampleRate;

    for (std::uint32_t frame = 0; frame < frames; ++frame)
    {
        const auto sample = self.noteOn ? static_cast<float>(std::sin(self.phase) * self.gain) : 0.0f;

        if (self.noteOn)
            self.phase += increment;

        for (std::uint32_t channel = 0; channel < output.channel_count; ++channel)
            output.data32[channel][frame] = sample;
    }

    output.constant_mask = 0;
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &audioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &notePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0)
        return &state;

    return nullptr;
}

void pluginOnMainThread(const clap_plugin_t*) {}

const clap_plugin_t* createPlugin(const clap_host_t* host)
{
    auto* self = new Plugin{};
    self->host = host;
    self->plugin.desc = &descriptor;
    self->plugin.plugin_data = self;
    self->plugin.init = pluginInit;
    self->plugin.destroy = pluginDestroy;
    self->plugin.activate = pluginActivate;
    self->plugin.deactivate = pluginDeactivate;
    self->plugin.start_processing = pluginStartProcessing;
    self->plugin.stop_processing = pluginStopProcessing;
    self->plugin.reset = pluginReset;
    self->plugin.process = pluginProcess;
    self->plugin.get_extension = pluginGetExtension;
    self->plugin.on_main_thread = pluginOnMainThread;
    return &self->plugin;
}

// --- factory and entry -----------------------------------------------------

std::uint32_t factoryCount(const clap_plugin_factory*)
{
    return 1;
}

const clap_plugin_descriptor_t* factoryDescriptor(const clap_plugin_factory*, std::uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}

const clap_plugin_t* factoryCreate(const clap_plugin_factory*, const clap_host_t* host, const char* id)
{
    if (host == nullptr || id == nullptr || std::strcmp(id, pluginId) != 0)
        return nullptr;

    return createPlugin(host);
}

const clap_plugin_factory_t factory = {factoryCount, factoryDescriptor, factoryCreate};

bool entryInit(const char*)
{
    return true;
}

void entryDeinit() {}

const void* entryGetFactory(const char* factoryId)
{
    if (factoryId != nullptr && std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
        return &factory;

    return nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    entryInit,
    entryDeinit,
    entryGetFactory,
};
