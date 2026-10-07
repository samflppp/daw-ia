#include "daw/engine/SampleIndex.h"

#include "daw/domain/serialization/Json.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <thread>

namespace daw::engine
{
namespace
{

// The longest a sample is read: a one-shot, not a song. A loop of minutes
// would be measured on its first ten seconds.
constexpr double longestSeconds = 10.0;

domain::Value entryValue(const SampleIndex::Entry& entry)
{
    return domain::Value::object(
        {{"path", domain::Value{entry.path}},
         {"bytes", domain::Value{entry.bytes}},
         {"modified", domain::Value{entry.modifiedMs}},
         {"role",
          entry.role ? domain::Value{std::string{domain::kit::nameOf(*entry.role)}} : domain::Value{}},
         {"roleToken",
          entry.role ? domain::Value{std::string{domain::kit::tokenOf(*entry.role)}} : domain::Value{}},
         {"features", entry.features.toValue()}});
}

} // namespace

SampleIndex::SampleIndex(juce::File store)
    : store_{std::move(store)}
{
}

std::optional<SampleIndex::Entry> SampleIndex::measureFile(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader{formats.createReaderFor(file)};
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return std::nullopt;

    const auto count = static_cast<int>(std::min<juce::int64>(
        reader->lengthInSamples, static_cast<juce::int64>(longestSeconds * reader->sampleRate)));
    juce::AudioBuffer<float> buffer{static_cast<int>(std::max(1U, std::min(2U, reader->numChannels))), count};
    if (!reader->read(&buffer, 0, count, 0, true, buffer.getNumChannels() > 1))
        return std::nullopt;

    Entry entry;
    entry.path = file.getFullPathName().toStdString();
    entry.bytes = file.getSize();
    entry.modifiedMs = file.getLastModificationTime().toMilliseconds();
    const auto* left = buffer.getReadPointer(0);
    const auto* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : left;
    entry.features = domain::kit::measure(left, right, static_cast<std::size_t>(count), reader->sampleRate);
    entry.role = domain::kit::roleOf(entry.path, entry.features);
    return entry;
}

std::vector<SampleIndex::Entry> SampleIndex::load() const
{
    std::vector<Entry> entries;
    if (!store_.existsAsFile())
        return entries;
    const auto read = domain::json::read(store_.loadFileAsString().toStdString());
    if (!read)
        return entries;
    const auto& root = read.value();
    const auto version = root.intAt("measureVersion");
    const auto* samples = root.find("samples");
    if (!version || version.value() != measureVersion || samples == nullptr || samples->asArray() == nullptr)
        return entries; // another measure, or nothing readable: measured again
    for (const auto& item : *samples->asArray())
    {
        Entry entry;
        const auto path = item.stringAt("path");
        const auto bytes = item.intAt("bytes");
        const auto modified = item.intAt("modified");
        const auto* features = item.find("features");
        if (!path || !bytes || !modified || features == nullptr)
            continue;
        auto measured = domain::kit::Features::fromValue(*features);
        if (!measured)
            continue;
        entry.path = path.value();
        entry.bytes = bytes.value();
        entry.modifiedMs = modified.value();
        entry.features = measured.value();
        if (const auto token = item.stringAt("roleToken"); token)
            entry.role = domain::kit::roleFromName(token.value());
        entries.push_back(std::move(entry));
    }
    return entries;
}

void SampleIndex::save(const std::vector<Entry>& entries) const
{
    domain::Value::Array samples;
    samples.reserve(entries.size());
    for (const auto& entry : entries)
        samples.push_back(entryValue(entry));
    const auto text =
        domain::json::write(domain::Value::object({{"measureVersion", domain::Value{measureVersion}},
                                                   {"samples", domain::Value::array(std::move(samples))}}));
    static_cast<void>(store_.getParentDirectory().createDirectory());
    static_cast<void>(store_.replaceWithText(juce::String::fromUTF8(text.c_str())));
}

SampleIndex::Built SampleIndex::build(const std::vector<juce::File>& files,
                                      const std::atomic<bool>& cancelled,
                                      const std::function<void(std::size_t, std::size_t)>& progress,
                                      int threads)
{
    const auto started = juce::Time::getMillisecondCounterHiRes();
    Built built;
    built.entries.resize(files.size());

    // What is already known, by path: kept when the size and the date agree.
    std::map<std::string, Entry> known;
    for (auto& entry : load())
        known.emplace(entry.path, std::move(entry));

    std::vector<std::size_t> toMeasure;
    std::vector<bool> filled(files.size(), false);
    for (std::size_t index = 0; index < files.size(); ++index)
    {
        const auto path = files[index].getFullPathName().toStdString();
        const auto found = known.find(path);
        if (found != known.end() && found->second.bytes == files[index].getSize() &&
            found->second.modifiedMs == files[index].getLastModificationTime().toMilliseconds())
        {
            built.entries[index] = found->second;
            filled[index] = true;
            ++built.reused;
        }
        else
            toMeasure.push_back(index);
    }

    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{built.reused};
    std::mutex counting;
    const auto work = [&]
    {
        while (!cancelled.load())
        {
            const auto at = next.fetch_add(1);
            if (at >= toMeasure.size())
                return;
            const auto index = toMeasure[at];
            auto measured = measureFile(files[index]);
            {
                const std::scoped_lock lock{counting};
                if (measured)
                {
                    built.entries[index] = std::move(*measured);
                    filled[index] = true;
                    ++built.measured;
                }
                else
                    ++built.unreadable;
            }
            const auto now = done.fetch_add(1) + 1;
            if (progress)
                progress(now, files.size());
        }
    };
    std::vector<std::thread> workers;
    const auto count = std::max(1, threads);
    for (int each = 1; each < count; ++each)
        workers.emplace_back(work);
    work();
    for (auto& worker : workers)
        worker.join();

    built.cancelled = cancelled.load();
    // What could not be read is left out, in the file and in the library.
    std::vector<Entry> kept;
    for (std::size_t index = 0; index < files.size(); ++index)
        if (filled[index])
            kept.push_back(built.entries[index]);
    built.entries = std::move(kept);
    if (!built.cancelled)
        save(built.entries);
    built.seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
    return built;
}

std::vector<domain::kit::Sample> SampleIndex::library(const Built& built)
{
    std::vector<domain::kit::Sample> samples;
    for (const auto& entry : built.entries)
        if (entry.role)
            samples.push_back(domain::kit::Sample{entry.path, *entry.role, entry.features});
    return samples;
}

} // namespace daw::engine
