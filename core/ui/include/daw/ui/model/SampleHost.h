#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/WaveformPeaks.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace daw::ui
{

// What the browser, the channel rack and the playlist cannot do by themselves
// with a sample: bring its bytes into the project, and know which folders of
// this machine hold the user's drumkits.
//
// Importing is not a command. The bytes go to the project's content store
// first, the way a plugin's state does, and the command that follows names
// them by digest — so the journal never holds a path to a drumkit that may
// move, and a replay never reads outside the project.
//
// The folders are not project state either: they are this machine's, like the
// plugins it has installed, and they are kept with the application's settings.
//
// It broadcasts when a waveform it was measuring is ready.
class SampleHost : public juce::ChangeBroadcaster
{
public:
    SampleHost() = default;
    ~SampleHost() override = default;

    SampleHost(const SampleHost&) = delete;
    SampleHost& operator=(const SampleHost&) = delete;
    SampleHost(SampleHost&&) = delete;
    SampleHost& operator=(SampleHost&&) = delete;

    // The extensions a sample may have, lowercase, without the dot.
    [[nodiscard]] static bool isSampleFile(const juce::File& file)
    {
        static const juce::StringArray formats{"wav", "aif", "aiff", "flac", "mp3", "ogg"};
        return formats.contains(file.getFileExtension().trimCharactersAtStart(".").toLowerCase());
    }

    // Copies the file's bytes into the project and measures it. Refused, and
    // said, for a file that is not audio this build can read.
    [[nodiscard]] virtual domain::Result<domain::SampleRef> import(const juce::File& file) = 0;

    // The folders the browser shows, in the order they were given.
    [[nodiscard]] virtual std::vector<juce::File> folders() const = 0;
    virtual void addFolder(const juce::File& folder) = 0;
    virtual void removeFolder(const juce::File& folder) = 0;

    // The shape of a sample the project holds, measured from its bytes.
    //
    // Nothing while it is being measured: the first call starts the work on
    // another thread and returns at once, and the host broadcasts when it is
    // done. Measured once per digest, however many clips play the sample.
    [[nodiscard]] virtual std::shared_ptr<const WaveformPeaks> waveform(const domain::SampleRef& sample) = 0;

    // How many samples were measured since the start, for the verification,
    // which has to prove that ten clips of one sample cost one measurement.
    [[nodiscard]] virtual std::size_t waveformsMeasured() const = 0;

    // Plays a sample of the machine to the speakers, before it is dropped:
    // nothing enters the project. A second audition stops the first.
    virtual void audition(const juce::File& file) = 0;
    virtual void stopAudition() = 0;

    // The sample being heard, and how loud what it sends out is now, in dBFS:
    // for the verification, which cannot listen.
    [[nodiscard]] virtual juce::File auditioned() const = 0;
    [[nodiscard]] virtual float auditionPeakDb() const = 0;
};

} // namespace daw::ui
