#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_core/juce_core.h>

#include <string>

namespace daw::engine
{

// Content-addressed store for the opaque state of hosted plugins.
//
// A plugin chunk can reach tens of megabytes, so it never travels inside a
// command payload nor inside ProjectState: the bytes land here, and the project
// keeps the BLAKE3 digest. Three properties follow from addressing by content,
// and the plugin model depends on all three:
//
//   deduplication  two instances carrying the same state are stored once.
//   immutability   a digest never designates other bytes, so undoing a capture
//                  is always possible: the previous blob is still there.
//   verification   a read recomputes the digest and refuses damaged bytes
//                  instead of handing them to a plugin.
//
// Nothing is ever deleted. Reclaiming unreferenced blobs needs the whole
// history to be known, which is what the versioning layer of S5 will bring.
class PluginStateStore
{
public:
    // root is created on demand, on the first put().
    explicit PluginStateStore(juce::File root);

    // The directory this store writes to, under the user's application data.
    [[nodiscard]] static juce::File defaultRoot(const juce::String& applicationName);

    [[nodiscard]] const juce::File& root() const noexcept { return root_; }

    [[nodiscard]] static std::string digestOf(const void* data, std::size_t size);

    // Writes the bytes unless they are already there, and returns their
    // reference. Empty input gives an empty reference: "no captured state".
    [[nodiscard]] domain::Result<domain::StateBlobRef> put(const void* data, std::size_t size);

    [[nodiscard]] domain::Result<juce::MemoryBlock> get(const domain::StateBlobRef& reference) const;

    [[nodiscard]] bool contains(const domain::StateBlobRef& reference) const;

private:
    [[nodiscard]] juce::File fileFor(const std::string& digest) const;

    juce::File root_;
};

} // namespace daw::engine
