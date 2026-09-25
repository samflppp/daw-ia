#include "daw/engine/Rendering.h"

namespace daw::engine
{

bool renderAsPlayed(tracktion::Edit& edit, const juce::File& file)
{
    static_cast<void>(file.deleteFile());

    tracktion::Renderer::Parameters parameters{edit};
    parameters.destFile = file;
    parameters.audioFormat = edit.engine.getAudioFileFormatManager().getWavFormat();
    parameters.bitDepth = 32;
    parameters.sampleRateForAudio = edit.engine.getDeviceManager().getSampleRate();
    parameters.blockSizeForAudio = edit.engine.getDeviceManager().getBlockSize();
    parameters.time = tracktion::TimeRange{tracktion::TimePosition{}, edit.getLength()};
    parameters.tracksToDo = tracktion::toBitSet(tracktion::getAllTracks(edit));
    parameters.usePlugins = true;
    parameters.useMasterPlugins = true;
    parameters.canRenderInMono = false;

    // The live graph lets go of the plugins for as long as the render holds
    // them, and gets them back after. Left running, the audio device and the
    // render would process the same plugin instances on two threads at once:
    // a voice of a synth, a sampler's position, a meter's totals would each
    // be written by both. The meters showed it, with 20 s of blocks too many
    // over a 100 s render.
    tracktion::TransportControl::stopAllTransports(edit.engine, false, true);
    const tracktion::TransportControl::ScopedContextAllocator restore{edit.getTransport()};
    edit.getTransport().freePlaybackContext();
    tracktion::Renderer::turnOffAllPlugins(edit);

    // The same loop Renderer::renderToFile runs when asked for no thread.
    auto task = tracktion::render_utils::createRenderTask(parameters, "render", nullptr, nullptr);
    if (task == nullptr)
        return false;

    while (task->runJob() == juce::ThreadPoolJob::jobNeedsRunningAgain)
    {
    }
    task.reset();

    tracktion::Renderer::turnOffAllPlugins(edit);
    return file.existsAsFile() && file.getSize() > 0;
}

} // namespace daw::engine
