#pragma once

#include "daw/engine/ContentStore.h"
#include "daw/ui/model/SampleHost.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include <functional>

namespace daw::app
{

// The application's answer to SampleHost: the project's content store for the
// bytes, the audio formats JUCE reads for the length, and the application's
// settings for the folders.
class SampleLibrary final : public ui::SampleHost
{
public:
    // The store is asked for at each import rather than held: the project, and
    // with it the store, can change while the application runs.
    SampleLibrary(std::function<engine::ContentStore*()> store, juce::PropertySet* settings);

    [[nodiscard]] domain::Result<domain::SampleRef> import(const juce::File& file) override;

    [[nodiscard]] std::vector<juce::File> folders() const override;
    void addFolder(const juce::File& folder) override;
    void removeFolder(const juce::File& folder) override;

private:
    void writeFolders(const std::vector<juce::File>& folders);

    std::function<engine::ContentStore*()> store_;
    juce::PropertySet* settings_{nullptr};
    juce::AudioFormatManager formats_;
};

} // namespace daw::app
