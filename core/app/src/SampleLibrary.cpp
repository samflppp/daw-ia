#include "SampleLibrary.h"

#include <algorithm>
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
