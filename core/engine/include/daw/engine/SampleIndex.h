#pragma once

#include "daw/domain/kit/Choice.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace daw::engine
{

// The index of the person's samples (S24): what each one measures
// (domain::kit::measure) and the role it plays, kept on the machine — never in
// a project — in one JSON file. A sample is known by its path, its size and
// its date, and by the version of the measure: one that did not change is not
// measured twice.
//
// Built off the message thread, on several threads, cancellable, with its
// progress; the file is written at the end of a build that was not
// cancelled.
class SampleIndex
{
public:
    // Bumped when the measure changes: every sample is measured again.
    static constexpr int measureVersion = 1;

    struct Entry
    {
        std::string path;
        std::int64_t bytes{0};
        std::int64_t modifiedMs{0};
        std::optional<domain::kit::Role> role;
        domain::kit::Features features;
    };

    struct Built
    {
        std::vector<Entry> entries; // in the order of the files given
        std::size_t measured{0};
        std::size_t reused{0};
        std::size_t unreadable{0};
        double seconds{0.0};
        bool cancelled{false};
    };

    explicit SampleIndex(juce::File store);

    // Any thread but the message one. `progress` is called from the workers
    // with the files done and the total.
    [[nodiscard]] Built build(const std::vector<juce::File>& files,
                              const std::atomic<bool>& cancelled,
                              const std::function<void(std::size_t, std::size_t)>& progress,
                              int threads);

    // The samples whose role is known, as the kit chooses from them.
    [[nodiscard]] static std::vector<domain::kit::Sample> library(const Built& built);

    [[nodiscard]] const juce::File& store() const noexcept { return store_; }

    // One file, measured: nothing when it is not audio this build reads.
    [[nodiscard]] static std::optional<Entry> measureFile(const juce::File& file);

private:
    [[nodiscard]] std::vector<Entry> load() const;
    void save(const std::vector<Entry>& entries) const;

    juce::File store_;
};

} // namespace daw::engine
