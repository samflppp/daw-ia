#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/project/ProjectState.h"

#include <juce_core/juce_core.h>

#include <string>

namespace daw::engine
{

// Content-addressed store: the bytes the project names by digest but never
// carries by value.
//
// The opaque state of a hosted plugin was the first user, and it set the
// shape: a chunk can reach tens of megabytes, so it never travels inside a
// command payload nor inside ProjectState. The bytes land here, and the
// project keeps the BLAKE3 digest. The context an agent acted upon — a prompt,
// a state extract, a model answer — is stored exactly the same way, by the
// same calls. The store is blind to provenance on purpose: an audio render
// produced by the generative engine and a take recorded by the user are the
// same thing here, a sequence of bytes named by its digest.
//
// Three properties follow from addressing by content, and the plugin model
// depends on all three:
//
//   deduplication  two instances carrying the same state are stored once.
//   immutability   a digest never designates other bytes, so undoing a capture
//                  is always possible: the previous blob is still there.
//   verification   a read recomputes the digest and refuses damaged bytes
//                  instead of handing them to a plugin.
//
// Nothing is ever deleted. Reclaiming unreferenced blobs needs the whole
// history to be known, which the journal now brings; the collector itself is
// not written, and the store is designed so that it can be.
class ContentStore
{
public:
    // root is created on demand, on the first put(). It is the blobs folder of
    // a project: the store is project state, not machine state, so that a
    // project folder copies whole and still plays.
    explicit ContentStore(juce::File root);

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
