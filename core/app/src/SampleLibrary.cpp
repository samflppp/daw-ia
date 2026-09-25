#include "SampleLibrary.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace daw::app
{
namespace
{

constexpr const char* foldersKey = "samples.folders";

} // namespace

SampleLibrary::SampleLibrary(std::function<engine::ContentStore*()> store, juce::PropertySet* settings)
    : store_(std::move(store))
    , settings_(settings)
{
    formats_.registerBasicFormats();
}

SampleLibrary::~SampleLibrary()
{
    alive_->store(false);
    pool_.removeAllJobs(true, 10000);
}

std::shared_ptr<const ui::WaveformPeaks> SampleLibrary::waveform(const domain::SampleRef& sample)
{
    const auto& digest = sample.blob.digest;
    if (const auto found = waveforms_.find(digest); found != waveforms_.end())
        return found->second;

    if (measuring_.count(digest) != 0)
        return nullptr;

    auto* store = store_ ? store_() : nullptr;
    auto* format = formats_.findFormatForFileExtension(juce::String::fromUTF8(sample.format.c_str()));
    if (store == nullptr || format == nullptr)
        return nullptr;

    measuring_.insert(digest);

    // Off the message thread: a sample of several minutes takes seconds to
    // read and decode, and the playlist must go on answering meanwhile. The
    // block is drawn empty and fills in when this comes back.
    pool_.addJob(
        [this, store, format, blob = sample.blob, alive = alive_]
        {
            auto peaks = std::make_shared<ui::WaveformPeaks>();

            if (auto bytes = store->get(blob); bytes)
            {
                // The stream owns its copy of the bytes, and the reader owns
                // the stream.
                juce::MemoryBlock block = std::move(bytes).value();
                std::unique_ptr<juce::AudioFormatReader> reader{
                    format->createReaderFor(new juce::MemoryInputStream(std::move(block)), true)};

                if (reader != nullptr && reader->sampleRate > 0.0 && reader->lengthInSamples > 0)
                {
                    // Bucket by bucket, straight from the reader: the whole
                    // sample is never held decoded.
                    const auto length = reader->lengthInSamples;
                    peaks->seconds = static_cast<double>(length) / reader->sampleRate;
                    const auto buckets = std::max<juce::int64>(
                        1,
                        static_cast<juce::int64>(std::ceil(
                            peaks->seconds * static_cast<double>(ui::WaveformPeaks::bucketsPerSecond))));
                    peaks->minimum.assign(static_cast<std::size_t>(buckets), 0.0f);
                    peaks->maximum.assign(static_cast<std::size_t>(buckets), 0.0f);

                    const auto channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2U));
                    const auto perBucket = static_cast<double>(length) / static_cast<double>(buckets);
                    for (juce::int64 bucket = 0; bucket < buckets && alive->load(); ++bucket)
                    {
                        const auto from = static_cast<juce::int64>(static_cast<double>(bucket) * perBucket);
                        const auto to = static_cast<juce::int64>(static_cast<double>(bucket + 1) * perBucket);
                        juce::Range<float> levels[2];
                        reader->readMaxLevels(from, std::max<juce::int64>(1, to - from), levels, channels);

                        auto low = levels[0].getStart();
                        auto high = levels[0].getEnd();
                        if (channels > 1)
                        {
                            low = std::min(low, levels[1].getStart());
                            high = std::max(high, levels[1].getEnd());
                        }
                        peaks->minimum[static_cast<std::size_t>(bucket)] = low;
                        peaks->maximum[static_cast<std::size_t>(bucket)] = high;
                    }
                }
            }

            juce::MessageManager::callAsync(
                [this, alive, digest = blob.digest, peaks]
                {
                    if (!alive->load())
                        return;

                    waveforms_[digest] = peaks;
                    measuring_.erase(digest);
                    ++measured_;
                    sendChangeMessage();
                });
        });

    return nullptr;
}

domain::Result<domain::SampleRef> SampleLibrary::import(const juce::File& file)
{
    using domain::ErrorCode;
    using domain::fail;

    auto* store = store_ ? store_() : nullptr;
    if (store == nullptr)
        return fail(ErrorCode::storageError, "no project is open to hold the sample");

    if (!file.existsAsFile() || !isSampleFile(file))
        return fail(ErrorCode::invalidArgument, "not a sample: " + file.getFileName().toStdString());

    // Measured before anything is stored: a file JUCE cannot read is refused
    // here, and never reaches the project as bytes nothing can play.
    std::unique_ptr<juce::AudioFormatReader> reader{formats_.createReaderFor(file)};
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return fail(ErrorCode::invalidArgument, "unreadable audio: " + file.getFileName().toStdString());

    const auto seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    reader.reset();

    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes))
        return fail(ErrorCode::storageError, "cannot read " + file.getFileName().toStdString());

    auto blob = store->put(bytes.getData(), bytes.getSize());
    if (!blob)
        return blob.error();

    domain::SampleRef sample{};
    sample.blob = blob.value();
    sample.name = file.getFileName().toStdString();
    sample.format = file.getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString();
    sample.seconds = seconds;

    if (auto valid = sample.validate(); !valid)
        return valid.error();

    return sample;
}

std::vector<juce::File> SampleLibrary::folders() const
{
    std::vector<juce::File> found;
    if (settings_ == nullptr)
        return found;

    const auto paths = juce::StringArray::fromLines(settings_->getValue(foldersKey));
    for (const auto& path : paths)
    {
        if (path.isNotEmpty())
            found.emplace_back(path);
    }
    return found;
}

void SampleLibrary::addFolder(const juce::File& folder)
{
    auto current = folders();
    if (!folder.isDirectory() || std::find(current.begin(), current.end(), folder) != current.end())
        return;

    current.push_back(folder);
    writeFolders(current);
}

void SampleLibrary::removeFolder(const juce::File& folder)
{
    auto current = folders();
    current.erase(std::remove(current.begin(), current.end(), folder), current.end());
    writeFolders(current);
}

void SampleLibrary::writeFolders(const std::vector<juce::File>& folders)
{
    if (settings_ == nullptr)
        return;

    juce::StringArray paths;
    for (const auto& folder : folders)
        paths.add(folder.getFullPathName());

    settings_->setValue(foldersKey, paths.joinIntoString("\n"));
}

} // namespace daw::app
