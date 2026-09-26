#pragma once

#include "SamplePreview.h"
#include "daw/engine/ContentStore.h"
#include "daw/ui/model/SampleHost.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

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
    ~SampleLibrary() override;

    SampleLibrary(const SampleLibrary&) = delete;
    SampleLibrary& operator=(const SampleLibrary&) = delete;
    SampleLibrary(SampleLibrary&&) = delete;
    SampleLibrary& operator=(SampleLibrary&&) = delete;

    [[nodiscard]] domain::Result<domain::SampleRef> import(const juce::File& file) override;

    [[nodiscard]] std::vector<juce::File> folders() const override;
    void addFolder(const juce::File& folder) override;
    void removeFolder(const juce::File& folder) override;

    [[nodiscard]] std::shared_ptr<const ui::WaveformPeaks> waveform(const domain::SampleRef& sample) override;
    [[nodiscard]] std::size_t waveformsMeasured() const override { return measured_.load(); }

    // The browser's preview, on the engine's audio device. Without one,
    // auditions are silently refused.
    void attachPreview(juce::AudioDeviceManager& device)
    {
        preview_ = std::make_unique<SamplePreview>(device);
    }

    void audition(const juce::File& file) override;
    void stopAudition() override;
    [[nodiscard]] juce::File auditioned() const override;
    [[nodiscard]] float auditionPeakDb() const override;

    [[nodiscard]] std::shared_ptr<const std::vector<ui::SearchEntry>> searchIndex() const override
    {
        return index_;
    }
    void indexSamples() override;

private:
    void writeFolders(const std::vector<juce::File>& folders);

    std::function<engine::ContentStore*()> store_;
    juce::PropertySet* settings_{nullptr};
    juce::AudioFormatManager formats_;

    // The waveforms, by digest, and the ones being measured. Both touched on
    // the message thread only: the measuring thread hands its result back
    // through callAsync, and the flag tells it whether anyone is still there.
    std::map<std::string, std::shared_ptr<const ui::WaveformPeaks>> waveforms_;
    std::set<std::string> measuring_;
    std::atomic<std::size_t> measured_{0};
    std::shared_ptr<std::atomic<bool>> alive_{std::make_shared<std::atomic<bool>>(true)};

    // One thread: measuring waits on the disk, and two samples measured at
    // once would only take turns on it.
    juce::ThreadPool pool_{1};

    std::unique_ptr<SamplePreview> preview_;

    // The search's list, replaced whole when a listing comes back. Listings
    // run on a thread of their own, so a big library never holds up the
    // waveforms; one asked while another runs makes the older one's result
    // stale, and it is dropped.
    std::shared_ptr<const std::vector<ui::SearchEntry>> index_;
    std::uint64_t indexAsked_{0};
    juce::ThreadPool indexPool_{1};
};

} // namespace daw::app
