#include "ClapPluginInstance.h"

#include "daw/engine/ClapPluginFormat.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>

namespace daw::engine::clap_host
{
namespace
{

constexpr std::size_t maximumEventBytes = 64 * 1024;
constexpr std::size_t maximumEventCount = 2048;

std::mutex& libraryCacheMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<juce::String, std::weak_ptr<EntryLibrary>>& libraryCache()
{
    static std::map<juce::String, std::weak_ptr<EntryLibrary>> cache;
    return cache;
}

bool hasFeature(const clap_plugin_descriptor_t& descriptor, const char* feature)
{
    if (descriptor.features == nullptr)
        return false;

    for (const auto* const* entry = descriptor.features; *entry != nullptr; ++entry)
    {
        if (std::strcmp(*entry, feature) == 0)
            return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// EntryLibrary
// ---------------------------------------------------------------------------

EntryLibrary::EntryLibrary(juce::File file, std::unique_ptr<juce::DynamicLibrary> library)
    : file_{std::move(file)}
    , library_{std::move(library)}
{
}

EntryLibrary::~EntryLibrary()
{
    if (entry_ != nullptr && entry_->deinit != nullptr)
        entry_->deinit();
}

std::shared_ptr<EntryLibrary> EntryLibrary::open(const juce::File& file)
{
    const std::lock_guard guard{libraryCacheMutex()};

    const auto key = file.getFullPathName();
    auto& cache = libraryCache();

    if (const auto existing = cache.find(key); existing != cache.end())
    {
        if (auto alive = existing->second.lock())
            return alive;
    }

    auto library = std::make_unique<juce::DynamicLibrary>();
    if (!library->open(key))
        return nullptr;

    // clap_entry is exported data, not a function, but the platform lookup is
    // the same one either way.
    const auto* entry = static_cast<const clap_plugin_entry_t*>(library->getFunction("clap_entry"));
    if (entry == nullptr || entry->init == nullptr || entry->get_factory == nullptr)
        return nullptr;

    if (!clap_version_is_compatible(entry->clap_version))
        return nullptr;

    // The plugin is given the path it was loaded from: several plugins resolve
    // their own resources relative to it.
    if (!entry->init(key.toRawUTF8()))
        return nullptr;

    const auto* factory =
        static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr || factory->get_plugin_count == nullptr || factory->create_plugin == nullptr)
    {
        if (entry->deinit != nullptr)
            entry->deinit();
        return nullptr;
    }

    std::shared_ptr<EntryLibrary> shared{new EntryLibrary{file, std::move(library)}};
    shared->entry_ = entry;
    shared->factory_ = factory;

    cache[key] = shared;
    return shared;
}

// ---------------------------------------------------------------------------
// ClapParameter
// ---------------------------------------------------------------------------

ClapParameter::ClapParameter(PluginInstance& owner, const clap_param_info_t& info)
    : owner_{owner}
    , id_{info.id}
    , name_{juce::String::fromUTF8(info.name)}
    , minimum_{info.min_value}
    , maximum_{info.max_value}
    , defaultValue_{info.default_value}
    , stepped_{(info.flags & CLAP_PARAM_IS_STEPPED) != 0}
    , automatable_{(info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0}
{
    if (!(maximum_ > minimum_))
        maximum_ = minimum_ + 1.0; // a degenerate range would divide by zero

    normalised_.store(toNormalised(defaultValue_), std::memory_order_relaxed);
}

double ClapParameter::toPlain(float normalised) const noexcept
{
    const auto clamped = juce::jlimit(0.0, 1.0, static_cast<double>(normalised));
    return minimum_ + clamped * (maximum_ - minimum_);
}

float ClapParameter::toNormalised(double plain) const noexcept
{
    const auto normalised = (plain - minimum_) / (maximum_ - minimum_);
    return static_cast<float>(juce::jlimit(0.0, 1.0, normalised));
}

float ClapParameter::getValue() const
{
    return normalised_.load(std::memory_order_relaxed);
}

void ClapParameter::setValue(float newValue)
{
    normalised_.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_relaxed);
    owner_.requestParameterChange(id_, toPlain(newValue));
}

void ClapParameter::storeFromPlugin(double plainValue) noexcept
{
    normalised_.store(toNormalised(plainValue), std::memory_order_relaxed);
}

float ClapParameter::getDefaultValue() const
{
    return toNormalised(defaultValue_);
}

juce::String ClapParameter::getName(int maximumStringLength) const
{
    return name_.substring(0, maximumStringLength);
}

int ClapParameter::getNumSteps() const
{
    if (!stepped_)
        return juce::AudioProcessor::getDefaultNumParameterSteps();

    const auto steps = static_cast<int>(maximum_ - minimum_) + 1;
    return std::max(steps, 2);
}

bool ClapParameter::isDiscrete() const
{
    return stepped_;
}

bool ClapParameter::isBoolean() const
{
    return stepped_ && juce::approximatelyEqual(maximum_ - minimum_, 1.0);
}

juce::String ClapParameter::getText(float normalisedValue, int maximumLength) const
{
    const auto* params = owner_.params();
    if (params != nullptr && params->value_to_text != nullptr)
    {
        std::array<char, CLAP_NAME_SIZE> text{};
        if (params->value_to_text(owner_.plugin(),
                                  id_,
                                  toPlain(normalisedValue),
                                  text.data(),
                                  static_cast<std::uint32_t>(text.size())))
            return juce::String::fromUTF8(text.data()).substring(0, maximumLength);
    }

    return juce::String(toPlain(normalisedValue), 3).substring(0, maximumLength);
}

float ClapParameter::getValueForText(const juce::String& text) const
{
    const auto* params = owner_.params();
    if (params != nullptr && params->text_to_value != nullptr)
    {
        double plain = 0.0;
        if (params->text_to_value(owner_.plugin(), id_, text.toRawUTF8(), &plain))
            return toNormalised(plain);
    }

    return toNormalised(text.getDoubleValue());
}

// ---------------------------------------------------------------------------
// PluginInstance — construction
// ---------------------------------------------------------------------------

PluginInstance& PluginInstance::from(const clap_host_t* host)
{
    return *static_cast<PluginInstance*>(host->host_data);
}

PluginInstance::PluginInstance(std::shared_ptr<EntryLibrary> library)
    : library_{std::move(library)}
{
    hostParams_.rescan = hostParamsRescan;
    hostParams_.clear = hostParamsClear;
    hostParams_.request_flush = hostParamsRequestFlush;

    hostState_.mark_dirty = hostStateMarkDirty;

    hostGui_.resize_hints_changed = hostGuiResizeHintsChanged;
    hostGui_.request_resize = hostGuiRequestResize;
    hostGui_.request_show = hostGuiRequestShow;
    hostGui_.request_hide = hostGuiRequestHide;
    hostGui_.closed = hostGuiClosed;

    hostThreadCheck_.is_main_thread = hostIsMainThread;
    hostThreadCheck_.is_audio_thread = hostIsAudioThread;

    hostNotePorts_.supported_dialects = hostNoteSupportedDialects;
    hostNotePorts_.rescan = hostNoteRescan;

    hostLog_.log = hostLog;

    host_.clap_version = CLAP_VERSION;
    host_.host_data = this;
    host_.name = "DAW IA";
    host_.vendor = "DAW IA";
    host_.url = "";
    host_.version = "0.1";
    host_.get_extension = hostGetExtension;
    host_.request_restart = hostRequestRestart;
    host_.request_process = hostRequestProcess;
    host_.request_callback = hostRequestCallback;

    inputEvents_.ctx = this;
    inputEvents_.size = inputEventsSize;
    inputEvents_.get = inputEventsGet;

    outputEvents_.ctx = this;
    outputEvents_.try_push = outputEventsTryPush;

    eventStorage_.resize(maximumEventBytes);
    eventOffsets_.reserve(maximumEventCount);
}

PluginInstance::~PluginInstance()
{
    cancelPendingUpdate();

    if (plugin_ != nullptr)
    {
        if (gui_ != nullptr && gui_->destroy != nullptr)
            gui_->destroy(plugin_);

        if (processing_ && plugin_->stop_processing != nullptr)
            plugin_->stop_processing(plugin_);

        if (active_ && plugin_->deactivate != nullptr)
            plugin_->deactivate(plugin_);

        if (plugin_->destroy != nullptr)
            plugin_->destroy(plugin_);
    }
}

std::unique_ptr<PluginInstance> PluginInstance::create(const juce::File& file,
                                                       const juce::String& clapId,
                                                       double sampleRate,
                                                       int blockSize,
                                                       juce::String& errorMessage)
{
    auto library = EntryLibrary::open(file);
    if (library == nullptr)
    {
        errorMessage = "not a usable CLAP library: " + file.getFullPathName();
        return nullptr;
    }

    std::unique_ptr<PluginInstance> instance{new PluginInstance{std::move(library)}};
    if (!instance->loadPlugin(clapId, errorMessage))
        return nullptr;

    instance->setPlayConfigDetails(instance->mainInputChannels_,
                                   instance->mainOutputChannels_,
                                   sampleRate > 0.0 ? sampleRate : 44100.0,
                                   blockSize > 0 ? blockSize : 512);
    return instance;
}

bool PluginInstance::loadPlugin(const juce::String& clapId, juce::String& errorMessage)
{
    const auto* factory = library_->factory();
    const auto count = factory->get_plugin_count(factory);

    const clap_plugin_descriptor_t* chosen = nullptr;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto* descriptor = factory->get_plugin_descriptor(factory, index);
        if (descriptor == nullptr || descriptor->id == nullptr)
            continue;

        // An empty id means "the first plugin of the file", which is what a
        // description written before this file gained a second plugin says.
        if (clapId.isEmpty() || clapId == juce::String::fromUTF8(descriptor->id))
        {
            chosen = descriptor;
            break;
        }
    }

    if (chosen == nullptr)
    {
        errorMessage = "no CLAP plugin with id " + clapId + " in " + library_->file().getFullPathName();
        return false;
    }

    clapId_ = juce::String::fromUTF8(chosen->id);
    name_ = chosen->name != nullptr ? juce::String::fromUTF8(chosen->name) : clapId_;
    vendor_ = chosen->vendor != nullptr ? juce::String::fromUTF8(chosen->vendor) : juce::String{};
    version_ = chosen->version != nullptr ? juce::String::fromUTF8(chosen->version) : juce::String{};
    isInstrument_ = hasFeature(*chosen, CLAP_PLUGIN_FEATURE_INSTRUMENT);
    isMidiEffect_ = hasFeature(*chosen, CLAP_PLUGIN_FEATURE_NOTE_EFFECT);

    plugin_ = factory->create_plugin(factory, &host_, chosen->id);
    if (plugin_ == nullptr)
    {
        errorMessage = "the CLAP factory refused to create " + clapId_;
        return false;
    }

    if (plugin_->init == nullptr || !plugin_->init(plugin_))
    {
        if (plugin_->destroy != nullptr)
            plugin_->destroy(plugin_);
        plugin_ = nullptr;
        errorMessage = clapId_ + " failed to initialise";
        return false;
    }

    readExtensions();
    readPorts();
    buildParameters();
    return true;
}

void PluginInstance::readExtensions()
{
    const auto get = [this](const char* id) { return plugin_->get_extension(plugin_, id); };

    params_ = static_cast<const clap_plugin_params_t*>(get(CLAP_EXT_PARAMS));
    state_ = static_cast<const clap_plugin_state_t*>(get(CLAP_EXT_STATE));
    audioPorts_ = static_cast<const clap_plugin_audio_ports_t*>(get(CLAP_EXT_AUDIO_PORTS));
    notePorts_ = static_cast<const clap_plugin_note_ports_t*>(get(CLAP_EXT_NOTE_PORTS));
    tail_ = static_cast<const clap_plugin_tail_t*>(get(CLAP_EXT_TAIL));

    if (const auto* gui = static_cast<const clap_plugin_gui_t*>(get(CLAP_EXT_GUI));
        gui != nullptr && gui->create != nullptr && gui->is_api_supported != nullptr)
    {
        // Embedded first: a plugin window that lives inside a host window is
        // what the rest of the application can place and remember. A plugin
        // that only does floating windows keeps its own.
        if (gui->is_api_supported(plugin_, nativeWindowApi(), false))
        {
            gui_ = gui;
            guiIsFloating_ = false;
        }
        else if (gui->is_api_supported(plugin_, nativeWindowApi(), true))
        {
            gui_ = gui;
            guiIsFloating_ = true;
        }
    }
}

void PluginInstance::readPorts()
{
    inputPortLayout_.clear();
    outputPortLayout_.clear();
    mainInputPort_ = -1;
    mainOutputPort_ = -1;
    mainInputChannels_ = 0;
    mainOutputChannels_ = 0;

    if (audioPorts_ != nullptr && audioPorts_->count != nullptr && audioPorts_->get != nullptr)
    {
        const auto read =
            [this](bool isInput, std::vector<PortLayout>& layout, int& mainPort, int& mainChannels)
        {
            const auto count = audioPorts_->count(plugin_, isInput);
            for (std::uint32_t index = 0; index < count; ++index)
            {
                clap_audio_port_info_t info{};
                if (!audioPorts_->get(plugin_, index, isInput, &info))
                    continue;

                const bool isMain = (info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0 || mainPort < 0;
                layout.push_back(PortLayout{info.channel_count, isMain && mainPort < 0});

                if (isMain && mainPort < 0)
                {
                    mainPort = static_cast<int>(index);
                    mainChannels = static_cast<int>(info.channel_count);
                }
            }
        };

        read(true, inputPortLayout_, mainInputPort_, mainInputChannels_);
        read(false, outputPortLayout_, mainOutputPort_, mainOutputChannels_);
    }

    if (outputPortLayout_.empty())
    {
        // A plugin without audio ports still has to be given somewhere to
        // write: the track's own buffer, in stereo.
        outputPortLayout_.push_back(PortLayout{2, true});
        mainOutputPort_ = 0;
        mainOutputChannels_ = 2;
    }

    if (notePorts_ != nullptr && notePorts_->count != nullptr && notePorts_->get != nullptr)
    {
        hasNoteInput_ = notePorts_->count(plugin_, true) > 0;
        hasNoteOutput_ = notePorts_->count(plugin_, false) > 0;

        clap_note_port_info_t info{};
        if (hasNoteInput_ && notePorts_->get(plugin_, 0, true, &info))
        {
            // Raw MIDI when the plugin takes it, CLAP note events otherwise.
            // Preferring MIDI keeps pitch bend, controllers and program changes
            // working without translating each one by hand.
            notePortTakesMidi_ = (info.supported_dialects & CLAP_NOTE_DIALECT_MIDI) != 0;
        }
    }
}

void PluginInstance::buildParameters()
{
    parameters_.clear();

    if (params_ == nullptr || params_->count == nullptr || params_->get_info == nullptr)
        return;

    const auto count = params_->count(plugin_);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        clap_param_info_t info{};
        if (!params_->get_info(plugin_, index, &info))
            continue;

        if ((info.flags & CLAP_PARAM_IS_HIDDEN) != 0)
            continue;

        auto parameter = std::make_unique<ClapParameter>(*this, info);
        parameters_.push_back(parameter.get());
        addHostedParameter(std::move(parameter));
    }

    refreshAllParameterValues();
}

void PluginInstance::refreshAllParameterValues()
{
    if (params_ == nullptr || params_->get_value == nullptr)
        return;

    for (auto* parameter : parameters_)
    {
        double plain = 0.0;
        if (params_->get_value(plugin_, parameter->clapId(), &plain))
            parameter->storeFromPlugin(plain);
    }
}

ClapParameter* PluginInstance::findParameter(clap_id id) const noexcept
{
    for (auto* parameter : parameters_)
    {
        if (parameter->clapId() == id)
            return parameter;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// PluginInstance — description and state
// ---------------------------------------------------------------------------

void PluginInstance::fillInPluginDescription(juce::PluginDescription& description) const
{
    description.name = name_;
    description.descriptiveName = name_;
    description.pluginFormatName = ClapPluginFormat::formatName;
    description.category = isInstrument_ ? "Synth" : "Effect";
    description.manufacturerName = vendor_;
    description.version = version_;
    description.fileOrIdentifier = ClapPluginFormat::makeFileOrIdentifier(library_->file(), clapId_);
    description.lastFileModTime = library_->file().getLastModificationTime();
    description.lastInfoUpdateTime = juce::Time::getCurrentTime();
    description.isInstrument = isInstrument_;
    description.numInputChannels = mainInputChannels_;
    description.numOutputChannels = mainOutputChannels_;
    description.hasSharedContainer = false;

    // A stable integer for JUCE's own bookkeeping. The identity this project
    // uses is the CLAP id string; this is only what juce::PluginDescription
    // asks for.
    description.uniqueId = static_cast<int>(clapId_.hashCode());
    description.deprecatedUid = description.uniqueId;
}

void PluginInstance::getStateInformation(juce::MemoryBlock& destination)
{
    destination.reset();

    if (state_ == nullptr || state_->save == nullptr)
        return;

    clap_ostream_t stream{};
    stream.ctx = &destination;
    stream.write = [](const clap_ostream_t* self, const void* buffer, std::uint64_t size) -> std::int64_t
    {
        auto* block = static_cast<juce::MemoryBlock*>(self->ctx);
        block->append(buffer, static_cast<std::size_t>(size));
        return static_cast<std::int64_t>(size);
    };

    if (!state_->save(plugin_, &stream))
        destination.reset();
}

void PluginInstance::setStateInformation(const void* data, int sizeInBytes)
{
    if (state_ == nullptr || state_->load == nullptr || data == nullptr || sizeInBytes <= 0)
        return;

    struct Reader
    {
        const std::byte* data{nullptr};
        std::size_t size{0};
        std::size_t position{0};
    };

    Reader reader{static_cast<const std::byte*>(data), static_cast<std::size_t>(sizeInBytes), 0};

    clap_istream_t stream{};
    stream.ctx = &reader;
    stream.read = [](const clap_istream_t* self, void* buffer, std::uint64_t size) -> std::int64_t
    {
        auto* source = static_cast<Reader*>(self->ctx);
        const auto remaining = source->size - source->position;
        const auto copied = std::min(static_cast<std::size_t>(size), remaining);
        std::memcpy(buffer, source->data + source->position, copied);
        source->position += copied;
        return static_cast<std::int64_t>(copied);
    };

    if (state_->load(plugin_, &stream))
    {
        // The plugin's parameters are now whatever the blob says, and the host
        // has to read them back rather than assume.
        refreshAllParameterValues();
        updateHostDisplay();
    }
}

const juce::String PluginInstance::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return name_;
}

void PluginInstance::changeProgramName(int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

double PluginInstance::getTailLengthSeconds() const
{
    if (tail_ != nullptr && tail_->get != nullptr)
    {
        const auto frames = tail_->get(plugin_);
        if (frames == 0)
            return 0.0;

        if (frames == std::numeric_limits<std::uint32_t>::max())
            return std::numeric_limits<double>::infinity();

        const auto rate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        return static_cast<double>(frames) / rate;
    }

    return isInstrument_ ? 0.0 : 0.0;
}

// ---------------------------------------------------------------------------
// PluginInstance — processing
// ---------------------------------------------------------------------------

void PluginInstance::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    if (plugin_ == nullptr)
        return;

    releaseResources();

    setRateAndBufferSizeDetails(sampleRate, maximumExpectedSamplesPerBlock);

    if (plugin_->activate != nullptr)
        active_ = plugin_->activate(
            plugin_, sampleRate, 1, static_cast<std::uint32_t>(std::max(1, maximumExpectedSamplesPerBlock)));

    // Every channel of every port the plugin declared, minus the two the track
    // provides, has to exist somewhere. Allocated once, here, never in
    // processBlock.
    int scratchChannels = 0;
    for (const auto& port : inputPortLayout_)
        scratchChannels += static_cast<int>(port.channelCount);
    for (const auto& port : outputPortLayout_)
    {
        if (!port.isMain)
            scratchChannels += static_cast<int>(port.channelCount);
    }

    scratch_.setSize(
        std::max(scratchChannels, 1), std::max(1, maximumExpectedSamplesPerBlock), false, true, true);

    inputBuffers_.assign(inputPortLayout_.size(), clap_audio_buffer_t{});
    outputBuffers_.assign(outputPortLayout_.size(), clap_audio_buffer_t{});

    std::size_t inputPointerCount = 0;
    for (const auto& port : inputPortLayout_)
        inputPointerCount += port.channelCount;

    std::size_t outputPointerCount = 0;
    for (const auto& port : outputPortLayout_)
        outputPointerCount += port.channelCount;

    inputChannelPointers_.assign(std::max<std::size_t>(inputPointerCount, 1), nullptr);
    outputChannelPointers_.assign(std::max<std::size_t>(outputPointerCount, 1), nullptr);

    producedMidi_.ensureSize(4096);
}

void PluginInstance::releaseResources()
{
    if (plugin_ == nullptr)
        return;

    if (processing_)
    {
        if (plugin_->stop_processing != nullptr)
            plugin_->stop_processing(plugin_);
        processing_ = false;
    }

    if (active_)
    {
        if (plugin_->deactivate != nullptr)
            plugin_->deactivate(plugin_);
        active_ = false;
    }
}

void PluginInstance::reset()
{
    if (plugin_ != nullptr && active_ && plugin_->reset != nullptr)
        plugin_->reset(plugin_);
}

void PluginInstance::pushEvent(const clap_event_header_t& event)
{
    if (eventOffsets_.size() >= maximumEventCount || eventBytesUsed_ + event.size > eventStorage_.size())
        return; // a block that overflows the list drops the surplus, never grows

    std::memcpy(eventStorage_.data() + eventBytesUsed_, &event, event.size);
    eventOffsets_.push_back(eventBytesUsed_);
    eventBytesUsed_ += event.size;
}

void PluginInstance::pushPending(const PendingChange& change)
{
    const auto write = pendingWrite_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % ringCapacity;
    if (next == pendingRead_.load(std::memory_order_acquire))
        return; // full: the newest movement is dropped, the audio thread is not

    pendingRing_[static_cast<std::size_t>(write)] = change;
    pendingWrite_.store(next, std::memory_order_release);
}

void PluginInstance::pushOutgoing(const OutgoingChange& change)
{
    const auto write = outgoingWrite_.load(std::memory_order_relaxed);
    const auto next = (write + 1) % ringCapacity;
    if (next == outgoingRead_.load(std::memory_order_acquire))
        return;

    outgoingRing_[static_cast<std::size_t>(write)] = change;
    outgoingWrite_.store(next, std::memory_order_release);
}

void PluginInstance::flushParameterChange(clap_id id, double plainValue)
{
    if (params_ == nullptr || params_->flush == nullptr)
        return;

    clap_event_param_value_t event{};
    event.header.size = sizeof(event);
    event.header.time = 0;
    event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    event.header.type = static_cast<std::uint16_t>(CLAP_EVENT_PARAM_VALUE);
    event.header.flags = 0;
    event.param_id = id;
    event.note_id = -1;
    event.port_index = -1;
    event.channel = -1;
    event.key = -1;
    event.value = plainValue;

    eventOffsets_.clear();
    eventBytesUsed_ = 0;
    pushEvent(event.header);

    params_->flush(plugin_, &inputEvents_, &outputEvents_);

    eventOffsets_.clear();
    eventBytesUsed_ = 0;
}

void PluginInstance::requestParameterChange(clap_id id, double plainValue)
{
    if (plugin_ == nullptr)
        return;

    if (processing_)
    {
        pushPending(PendingChange{id, plainValue});
        return;
    }

    // Not processing: the change has nowhere to ride, so it goes through
    // params.flush, which exists exactly for that case.
    flushParameterChange(id, plainValue);
}

void PluginInstance::collectInputEvents(const juce::MidiBuffer& midi, int numSamples)
{
    eventOffsets_.clear();
    eventBytesUsed_ = 0;

    // ClapParameter movements first, at frame zero: a value the user set before the
    // block started must be in force for the whole block.
    auto read = pendingRead_.load(std::memory_order_relaxed);
    const auto write = pendingWrite_.load(std::memory_order_acquire);
    while (read != write)
    {
        const auto& change = pendingRing_[static_cast<std::size_t>(read)];

        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = 0;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = static_cast<std::uint16_t>(CLAP_EVENT_PARAM_VALUE);
        event.header.flags = 0;
        event.param_id = change.id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = change.plainValue;
        pushEvent(event.header);

        read = (read + 1) % ringCapacity;
    }
    pendingRead_.store(read, std::memory_order_release);

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto time =
            static_cast<std::uint32_t>(juce::jlimit(0, std::max(0, numSamples - 1), metadata.samplePosition));

        if (notePortTakesMidi_ && message.getRawDataSize() <= 3)
        {
            clap_event_midi_t event{};
            event.header.size = sizeof(event);
            event.header.time = time;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = static_cast<std::uint16_t>(CLAP_EVENT_MIDI);
            event.header.flags = 0;
            event.port_index = 0;

            const auto* raw = message.getRawData();
            for (int byte = 0; byte < message.getRawDataSize(); ++byte)
                event.data[byte] = raw[byte];

            pushEvent(event.header);
            continue;
        }

        if (message.isNoteOnOrOff())
        {
            clap_event_note_t event{};
            event.header.size = sizeof(event);
            event.header.time = time;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type =
                static_cast<std::uint16_t>(message.isNoteOn() ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF);
            event.header.flags = 0;
            event.note_id = -1;
            event.port_index = 0;
            event.channel = static_cast<std::int16_t>(message.getChannel() - 1);
            event.key = static_cast<std::int16_t>(message.getNoteNumber());
            event.velocity = message.isNoteOn() ? message.getVelocity() / 127.0 : 0.0;
            pushEvent(event.header);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            clap_event_note_t event{};
            event.header.size = sizeof(event);
            event.header.time = time;
            event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            event.header.type = static_cast<std::uint16_t>(CLAP_EVENT_NOTE_CHOKE);
            event.header.flags = 0;
            event.note_id = -1;
            event.port_index = 0;
            event.channel = -1;
            event.key = -1;
            event.velocity = 0.0;
            pushEvent(event.header);
        }
    }
}

void PluginInstance::prepareAudioPorts(juce::AudioBuffer<float>& buffer)
{
    const auto numSamples = buffer.getNumSamples();
    int scratchChannel = 0;
    std::size_t pointerIndex = 0;

    for (std::size_t port = 0; port < inputPortLayout_.size(); ++port)
    {
        const auto channels = static_cast<int>(inputPortLayout_[port].channelCount);
        const auto first = pointerIndex;

        for (int channel = 0; channel < channels; ++channel)
        {
            auto* destination = scratch_.getWritePointer(scratchChannel++);

            const bool fromTrack =
                static_cast<int>(port) == mainInputPort_ && channel < buffer.getNumChannels();
            if (fromTrack)
                std::memcpy(destination,
                            buffer.getReadPointer(channel),
                            sizeof(float) * static_cast<std::size_t>(numSamples));
            else
                juce::FloatVectorOperations::clear(destination, numSamples);

            inputChannelPointers_[pointerIndex++] = destination;
        }

        inputBuffers_[port].data32 = channels > 0 ? inputChannelPointers_.data() + first : nullptr;
        inputBuffers_[port].data64 = nullptr;
        inputBuffers_[port].channel_count = static_cast<std::uint32_t>(channels);
        inputBuffers_[port].latency = 0;
        inputBuffers_[port].constant_mask = 0;
    }

    pointerIndex = 0;
    for (std::size_t port = 0; port < outputPortLayout_.size(); ++port)
    {
        const auto channels = static_cast<int>(outputPortLayout_[port].channelCount);
        const auto first = pointerIndex;

        for (int channel = 0; channel < channels; ++channel)
        {
            const bool toTrack =
                static_cast<int>(port) == mainOutputPort_ && channel < buffer.getNumChannels();
            outputChannelPointers_[pointerIndex++] =
                toTrack ? buffer.getWritePointer(channel) : scratch_.getWritePointer(scratchChannel++);
        }

        outputBuffers_[port].data32 = channels > 0 ? outputChannelPointers_.data() + first : nullptr;
        outputBuffers_[port].data64 = nullptr;
        outputBuffers_[port].channel_count = static_cast<std::uint32_t>(channels);
        outputBuffers_[port].latency = 0;
        outputBuffers_[port].constant_mask = 0;
    }
}

void PluginInstance::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    if (plugin_ == nullptr || !active_)
    {
        buffer.clear();
        midiMessages.clear();
        return;
    }

    if (buffer.getNumSamples() > scratch_.getNumSamples())
    {
        // A block larger than what activate() was told about is not something
        // to guess at: silence is honest, and prepareToPlay will be called
        // again with the real size.
        buffer.clear();
        return;
    }

    if (!processing_)
    {
        if (plugin_->start_processing != nullptr && !plugin_->start_processing(plugin_))
        {
            buffer.clear();
            return;
        }
        processing_ = true;
    }

    insideProcess_.store(true, std::memory_order_release);

    collectInputEvents(midiMessages, buffer.getNumSamples());
    prepareAudioPorts(buffer);
    producedMidi_.clear();

    clap_process_t process{};
    process.steady_time = -1;
    process.frames_count = static_cast<std::uint32_t>(buffer.getNumSamples());
    process.transport = nullptr;
    process.audio_inputs = inputBuffers_.empty() ? nullptr : inputBuffers_.data();
    process.audio_outputs = outputBuffers_.empty() ? nullptr : outputBuffers_.data();
    process.audio_inputs_count = static_cast<std::uint32_t>(inputBuffers_.size());
    process.audio_outputs_count = static_cast<std::uint32_t>(outputBuffers_.size());
    process.in_events = &inputEvents_;
    process.out_events = &outputEvents_;

    const auto status = plugin_->process(plugin_, &process);

    insideProcess_.store(false, std::memory_order_release);

    if (status == CLAP_PROCESS_ERROR)
        buffer.clear();

    midiMessages.swapWith(producedMidi_);

    if (outgoingRead_.load(std::memory_order_relaxed) != outgoingWrite_.load(std::memory_order_acquire))
        triggerAsyncUpdate();
}

void PluginInstance::handleAsyncUpdate()
{
    // Everything the plugin changed by itself reaches the host here, on the
    // message thread: gesture begin, values, gesture end. The bus refuses any
    // other thread, and Tracktion expects the same one, so this is the only
    // place where a plugin's own knob movement can become a command.
    auto read = outgoingRead_.load(std::memory_order_relaxed);
    const auto write = outgoingWrite_.load(std::memory_order_acquire);

    while (read != write)
    {
        const auto change = outgoingRing_[static_cast<std::size_t>(read)];
        read = (read + 1) % ringCapacity;

        if (auto* parameter = findParameter(change.id); parameter != nullptr)
        {
            switch (change.kind)
            {
            case OutgoingChange::Kind::gestureBegin:
                parameter->beginChangeGesture();
                break;

            case OutgoingChange::Kind::gestureEnd:
                parameter->endChangeGesture();
                break;

            case OutgoingChange::Kind::value:
                parameter->storeFromPlugin(change.plainValue);
                parameter->sendValueChangedMessageToListeners(parameter->toNormalised(change.plainValue));
                break;
            }
        }
    }

    outgoingRead_.store(read, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// PluginInstance — C callbacks
// ---------------------------------------------------------------------------

std::uint32_t PluginInstance::inputEventsSize(const clap_input_events_t* list)
{
    const auto& self = *static_cast<const PluginInstance*>(list->ctx);
    return static_cast<std::uint32_t>(self.eventOffsets_.size());
}

const clap_event_header_t* PluginInstance::inputEventsGet(const clap_input_events_t* list,
                                                          std::uint32_t index)
{
    const auto& self = *static_cast<const PluginInstance*>(list->ctx);
    if (index >= self.eventOffsets_.size())
        return nullptr;

    return reinterpret_cast<const clap_event_header_t*>(self.eventStorage_.data() +
                                                        self.eventOffsets_[index]);
}

bool PluginInstance::outputEventsTryPush(const clap_output_events_t* list, const clap_event_header_t* event)
{
    auto& self = *static_cast<PluginInstance*>(list->ctx);
    if (event == nullptr || event->space_id != CLAP_CORE_EVENT_SPACE_ID)
        return false;

    switch (event->type)
    {
    case CLAP_EVENT_PARAM_VALUE:
    {
        const auto& value = *reinterpret_cast<const clap_event_param_value_t*>(event);
        self.pushOutgoing(OutgoingChange{OutgoingChange::Kind::value, value.param_id, value.value});
        return true;
    }

    case CLAP_EVENT_PARAM_GESTURE_BEGIN:
    {
        const auto& gesture = *reinterpret_cast<const clap_event_param_gesture_t*>(event);
        self.pushOutgoing(OutgoingChange{OutgoingChange::Kind::gestureBegin, gesture.param_id, 0.0});
        return true;
    }

    case CLAP_EVENT_PARAM_GESTURE_END:
    {
        const auto& gesture = *reinterpret_cast<const clap_event_param_gesture_t*>(event);
        self.pushOutgoing(OutgoingChange{OutgoingChange::Kind::gestureEnd, gesture.param_id, 0.0});
        return true;
    }

    case CLAP_EVENT_MIDI:
    {
        const auto& midi = *reinterpret_cast<const clap_event_midi_t*>(event);
        self.producedMidi_.addEvent(midi.data, 3, static_cast<int>(event->time));
        return true;
    }

    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF:
    {
        const auto& note = *reinterpret_cast<const clap_event_note_t*>(event);
        const auto channel = note.channel < 0 ? 1 : note.channel + 1;
        const auto key = note.key < 0 ? 60 : note.key;
        const auto message = event->type == CLAP_EVENT_NOTE_ON
                                 ? juce::MidiMessage::noteOn(channel, key, static_cast<float>(note.velocity))
                                 : juce::MidiMessage::noteOff(channel, key);
        self.producedMidi_.addEvent(message, static_cast<int>(event->time));
        return true;
    }

    default:
        // Note expressions, modulation and the rest are accepted and
        // ignored: refusing them makes some plugins log an error on every
        // block for something this host never promised.
        return true;
    }
}

const void* PluginInstance::hostGetExtension(const clap_host_t* host, const char* extensionId)
{
    auto& self = from(host);

    if (std::strcmp(extensionId, CLAP_EXT_PARAMS) == 0)
        return &self.hostParams_;
    if (std::strcmp(extensionId, CLAP_EXT_STATE) == 0)
        return &self.hostState_;
    if (std::strcmp(extensionId, CLAP_EXT_GUI) == 0)
        return &self.hostGui_;
    if (std::strcmp(extensionId, CLAP_EXT_THREAD_CHECK) == 0)
        return &self.hostThreadCheck_;
    if (std::strcmp(extensionId, CLAP_EXT_NOTE_PORTS) == 0)
        return &self.hostNotePorts_;
    if (std::strcmp(extensionId, CLAP_EXT_LOG) == 0)
        return &self.hostLog_;

    return nullptr;
}

void PluginInstance::hostRequestRestart(const clap_host_t* host)
{
    auto& self = from(host);

    // A restart has to happen where activate() is legal, never from inside
    // process().
    juce::MessageManager::callAsync(
        [&self]
        {
            const auto rate = self.getSampleRate();
            const auto block = self.getBlockSize();
            if (rate > 0.0 && block > 0)
                self.prepareToPlay(rate, block);
        });
}

void PluginInstance::hostRequestProcess(const clap_host_t* host)
{
    juce::ignoreUnused(host); // the graph runs continuously; nothing to wake
}

void PluginInstance::hostRequestCallback(const clap_host_t* host)
{
    auto& self = from(host);
    juce::MessageManager::callAsync(
        [&self]
        {
            if (self.plugin_ != nullptr && self.plugin_->on_main_thread != nullptr)
                self.plugin_->on_main_thread(self.plugin_);
        });
}

bool PluginInstance::hostIsMainThread(const clap_host_t* host)
{
    return std::this_thread::get_id() == from(host).mainThread_;
}

bool PluginInstance::hostIsAudioThread(const clap_host_t* host)
{
    return from(host).insideProcess_.load(std::memory_order_acquire);
}

void PluginInstance::hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags flags)
{
    auto& self = from(host);

    if ((flags & (CLAP_PARAM_RESCAN_ALL | CLAP_PARAM_RESCAN_INFO)) != 0)
    {
        // The parameter list itself changed. Rebuilding it would invalidate
        // every paramId the project holds, so the values are refreshed and the
        // change is reported; a full rebuild belongs to a reload.
        juce::MessageManager::callAsync(
            [&self]
            {
                self.refreshAllParameterValues();
                self.updateHostDisplay(
                    juce::AudioProcessorListener::ChangeDetails{}.withParameterInfoChanged(true));
            });
        return;
    }

    juce::MessageManager::callAsync([&self] { self.refreshAllParameterValues(); });
}

void PluginInstance::hostParamsClear(const clap_host_t* host, clap_id paramId, clap_param_clear_flags flags)
{
    juce::ignoreUnused(host, paramId, flags); // no automation lives in the plugin
}

void PluginInstance::hostParamsRequestFlush(const clap_host_t* host)
{
    auto& self = from(host);
    if (self.processing_)
        return; // the next block carries the changes anyway

    juce::MessageManager::callAsync(
        [&self]
        {
            if (self.params_ != nullptr && self.params_->flush != nullptr && !self.processing_)
            {
                self.eventOffsets_.clear();
                self.eventBytesUsed_ = 0;
                self.params_->flush(self.plugin_, &self.inputEvents_, &self.outputEvents_);
                self.triggerAsyncUpdate();
            }
        });
}

void PluginInstance::hostStateMarkDirty(const clap_host_t* host)
{
    auto& self = from(host);

    // The plugin says its opaque state changed. The project does not capture it
    // here: a capture is a command, and it happens on saving or on closing the
    // session, not on every knob the plugin touches.
    juce::MessageManager::callAsync([&self] { self.updateHostDisplay(); });
}

void PluginInstance::hostGuiResizeHintsChanged(const clap_host_t* host)
{
    juce::ignoreUnused(host);
}

bool PluginInstance::hostGuiRequestResize(const clap_host_t* host, std::uint32_t width, std::uint32_t height)
{
    auto& self = from(host);
    if (auto* editor = self.getActiveEditor(); editor != nullptr)
    {
        juce::MessageManager::callAsync(
            [editor, width, height] { editor->setSize(static_cast<int>(width), static_cast<int>(height)); });
        return true;
    }
    return false;
}

bool PluginInstance::hostGuiRequestShow(const clap_host_t* host)
{
    juce::ignoreUnused(host);
    return false; // opening a window is the application's decision, not the plugin's
}

bool PluginInstance::hostGuiRequestHide(const clap_host_t* host)
{
    juce::ignoreUnused(host);
    return false;
}

void PluginInstance::hostGuiClosed(const clap_host_t* host, bool wasDestroyed)
{
    juce::ignoreUnused(host, wasDestroyed);
}

std::uint32_t PluginInstance::hostNoteSupportedDialects(const clap_host_t* host)
{
    juce::ignoreUnused(host);
    return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
}

void PluginInstance::hostNoteRescan(const clap_host_t* host, std::uint32_t flags)
{
    juce::ignoreUnused(flags);
    auto& self = from(host);
    juce::MessageManager::callAsync([&self] { self.readPorts(); });
}

void PluginInstance::hostLog(const clap_host_t* host, clap_log_severity severity, const char* message)
{
    if (severity < CLAP_LOG_WARNING || message == nullptr)
        return;

    juce::Logger::writeToLog("CLAP " + from(host).name_ + ": " + juce::String::fromUTF8(message));
}

const char* PluginInstance::nativeWindowApi()
{
#if JUCE_WINDOWS
    return CLAP_WINDOW_API_WIN32;
#elif JUCE_MAC
    return CLAP_WINDOW_API_COCOA;
#else
    return CLAP_WINDOW_API_X11;
#endif
}

} // namespace daw::engine::clap_host
