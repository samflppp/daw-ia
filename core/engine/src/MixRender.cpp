#include "daw/engine/MixRender.h"

#include "daw/engine/FluxTaps.h"
#include "daw/engine/MeterTap.h"
#include "daw/engine/MixTap.h"
#include "daw/engine/ProjectProjector.h"

#include <chrono>
#include <utility>

namespace daw::engine
{
namespace
{

const juce::Identifier domainTrackIdProperty{"dawDomainTrackId"};
const juce::Identifier domainRoleProperty{"dawDomainRole"};

double nowMs()
{
    return juce::Time::getMillisecondCounterHiRes();
}

} // namespace

struct MixRender::Slots
{
    // One analyser per tap: a domain track, its recordings' companion, or the
    // master. Owned here, looked up by the taps through MixTap's registry.
    std::map<juce::String, std::unique_ptr<domain::mix::StreamAnalyser>> analysers;

    // Every key of this render starts with it: two renders alive at once — the
    // project and a proposal — never share a tap's analyser.
    juce::String prefix{juce::Uuid{}.toString() + "/"};
    std::vector<MixTap::Registration> registrations;
    std::unique_ptr<tracktion::Renderer::RenderTask> task;
    juce::File file;
    std::atomic<float> progress{0.0f};
};

std::unique_ptr<MixRender> MixRender::prepare(tracktion::Edit& live,
                                              const domain::ProjectState& state,
                                              const domain::ProjectState* proposed,
                                              PluginCatalogue* catalogue,
                                              ContentStore* store)
{
    const auto started = nowMs();
    std::unique_ptr<MixRender> render{new MixRender};

    // The copy sounds as the session does now: plugin states are flushed into
    // the tree first, including what a plugin holds between two captures.
    live.flushState();
    tracktion::Edit::Options options{live.engine,
                                     live.state.createCopy(),
                                     tracktion::ProjectItemID::createNewID(tracktion::ProjectID{}),
                                     tracktion::Edit::forRendering};
    options.numUndoLevelsToStore = 0;
    options.numAudioTracks = 0;
    render->copy_ = tracktion::Edit::createEdit(std::move(options));
    if (render->copy_ == nullptr)
        return {};

    // Always the song, as it is laid out on the timeline: the beatmaker
    // opens in pattern mode, where the session plays the pattern alone and
    // the Edit is as long as nothing. Stopped, not looping, from the start.
    render->proposed_ = std::make_unique<domain::ProjectState>(proposed != nullptr ? *proposed : state);
    static_cast<void>(render->proposed_->setPlaying(false));
    static_cast<void>(render->proposed_->setPlayMode(domain::PlayMode::song, domain::PatternId{}));
    if (render->proposed_->transport().looping)
        static_cast<void>(render->proposed_->setLoop(false,
                                                     render->proposed_->transport().loopStartBeats,
                                                     render->proposed_->transport().loopEndBeats));
    static_cast<void>(render->proposed_->setPositionBeats(0.0));
    render->projector_ =
        std::make_unique<ProjectProjector>(*render->copy_, *render->proposed_, catalogue, store);
    render->projector_->reconcile();
    const domain::ProjectState* measured = render->proposed_.get();

    render->slots_ = std::make_unique<Slots>();
    auto& slots = *render->slots_;
    const auto sampleRate = live.engine.getDeviceManager().getSampleRate();

    const auto tap =
        [&slots, &render, sampleRate](tracktion::PluginList& list, int index, const juce::String& key)
    {
        slots.analysers[key] = std::make_unique<domain::mix::StreamAnalyser>(sampleRate);
        slots.registrations.push_back(MixTap::enrol(slots.prefix + key, *slots.analysers[key]));
        if (auto plugin = render->copy_->getPluginCache().createNewPlugin(
                MixTap::create(slots.prefix + key, render->copy_->getLength().inSeconds()));
            plugin != nullptr)
            list.insertPlugin(plugin, index, nullptr);
    };

    // Each track after its inserts, before its fader: the plugins the
    // projection leads with and the user's are before the volume plugin,
    // the sends and the meter after it.
    for (auto* track : tracktion::getAudioTracks(*render->copy_))
    {
        const auto id = track->state.getProperty(domainTrackIdProperty).toString();
        const auto parsed = domain::TrackId::parse(id.toStdString());
        if (!parsed || measured->findTrack(parsed.value()) == nullptr)
            continue;
        const auto companion = track->state.getProperty(domainRoleProperty).toString().isNotEmpty();
        auto* volume = track->getVolumePlugin();
        const auto at = volume != nullptr ? track->pluginList.indexOf(volume) : -1;
        tap(track->pluginList, at, companion ? id + ":audio" : id);
    }
    tap(render->copy_->getMasterPluginList(), -1, "master");

    // The render of the song as it plays, master plugins included; the file
    // is only where the renderer writes, the measure is what the taps heard.
    slots.file = juce::File::createTempFile(".wav");
    tracktion::Renderer::Parameters parameters{*render->copy_};
    parameters.destFile = slots.file;
    parameters.audioFormat = live.engine.getAudioFileFormatManager().getWavFormat();
    parameters.bitDepth = 32;
    parameters.sampleRateForAudio = sampleRate;
    // Larger than a device block: offline, fewer blocks are fewer hand-overs
    // between the render threads (34 s down to 30 s on sixteen tracks).
    parameters.blockSizeForAudio = 1024;
    parameters.time = tracktion::TimeRange{tracktion::TimePosition{}, render->copy_->getLength()};
    parameters.tracksToDo = tracktion::toBitSet(tracktion::getAllTracks(*render->copy_));
    parameters.usePlugins = true;
    parameters.useMasterPlugins = true;
    parameters.canRenderInMono = false;
    slots.task = tracktion::render_utils::createRenderTask(parameters, "mix", &slots.progress, nullptr);
    if (slots.task == nullptr)
        return {};

    render->prepareMs_ = nowMs() - started;
    return render;
}

juce::File MixRender::releaseFile()
{
    if (slots_ == nullptr || !finished_)
        return {};
    return std::exchange(slots_->file, juce::File{});
}

MixRender::~MixRender()
{
    if (slots_ != nullptr)
    {
        slots_->task.reset();
        static_cast<void>(slots_->file.deleteFile());
    }
    projector_.reset();
    copy_.reset();
}

void MixRender::captureFlux(double fromSeconds, double seconds)
{
    if (copy_ == nullptr)
        return;
    fluxRate_ = copy_->engine.getDeviceManager().getSampleRate();
    const auto from = static_cast<std::int64_t>(std::llround(std::max(0.0, fromSeconds) * fluxRate_));
    const auto samples = static_cast<int>(std::llround(std::max(0.0, seconds) * fluxRate_));
    for (auto* tap : FluxTaps{*copy_}.all())
        tap->startCapture(samples, from);
}

std::map<std::pair<std::string, std::string>, std::vector<float>> MixRender::fluxCaptured() const
{
    std::map<std::pair<std::string, std::string>, std::vector<float>> places;
    if (copy_ == nullptr)
        return places;
    for (const auto* tap : FluxTaps{*copy_}.all())
    {
        const auto& captured = tap->captured();
        if (captured.empty())
            continue;
        auto strip = tap->strip().toStdString();
        if (tap->strip() == MeterTapPlugin::masterStrip)
            strip = domain::ProjectState::masterTrackId().toString();
        auto& samples = places[{strip, tap->slot().toStdString()}];
        samples.resize(std::max(samples.size(), captured.size()), 0.0f);
        for (std::size_t index = 0; index < captured.size(); ++index)
            samples[index] += captured[index];
    }
    return places;
}

std::unique_ptr<MixRender::Measured> MixRender::run(const std::atomic<bool>& cancelled,
                                                    const std::function<void(double)>& progress)
{
    if (slots_ == nullptr || slots_->task == nullptr)
        return {};

    const auto started = nowMs();
    while (slots_->task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
    {
        if (cancelled.load(std::memory_order_relaxed))
            return {};
        if (progress)
            progress(static_cast<double>(slots_->progress.load(std::memory_order_relaxed)));
    }
    finished_ = true;

    auto measured = std::make_unique<Measured>();
    std::map<std::string, domain::mix::StreamMeasure> companions;
    for (const auto& [key, analyser] : slots_->analysers)
    {
        auto measure = analyser->finish();
        if (key == "master")
            measured->master = std::move(measure);
        else if (key.endsWith(":audio"))
            companions[key.upToLastOccurrenceOf(":audio", false, false).toStdString()] = std::move(measure);
        else
            measured->tracks[key.toStdString()] = std::move(measure);
    }
    for (auto& [id, recordings] : companions)
    {
        auto found = measured->tracks.find(id);
        if (found == measured->tracks.end())
            measured->tracks[id] = std::move(recordings);
        else
            found->second = domain::mix::combine(found->second, recordings);
    }
    measured->renderSeconds = (nowMs() - started) / 1000.0;
    return measured;
}

} // namespace daw::engine
