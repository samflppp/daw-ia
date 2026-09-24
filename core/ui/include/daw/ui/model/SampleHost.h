#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_core/juce_core.h>

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
class SampleHost
{
public:
    SampleHost() = default;
    virtual ~SampleHost() = default;

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
};

} // namespace daw::ui
