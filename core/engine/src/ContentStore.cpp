#include "daw/engine/ContentStore.h"

#include <array>
#include <cstdint>
#include <utility>

#include <blake3.h>

namespace daw::engine
{
namespace
{

constexpr std::size_t digestBytes = 32; // BLAKE3-256, the default output length

std::string toHex(const std::array<std::uint8_t, digestBytes>& bytes)
{
    static constexpr char alphabet[] = "0123456789abcdef";

    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const auto byte : bytes)
    {
        hex.push_back(alphabet[byte >> 4]);
        hex.push_back(alphabet[byte & 0x0f]);
    }
    return hex;
}

} // namespace

ContentStore::ContentStore(juce::File root)
    : root_{std::move(root)}
{
}

std::string ContentStore::digestOf(const void* data, std::size_t size)
{
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, size);

    std::array<std::uint8_t, digestBytes> output{};
    blake3_hasher_finalize(&hasher, output.data(), output.size());

    return toHex(output);
}

juce::File ContentStore::fileFor(const std::string& digest) const
{
    // Two characters of fan-out: a project with thousands of captures does not
    // put thousands of entries in one directory.
    const auto name = juce::String(digest);
    return root_.getChildFile(name.substring(0, 2)).getChildFile(name);
}

domain::Result<domain::StateBlobRef> ContentStore::put(const void* data, std::size_t size)
{
    if (data == nullptr || size == 0)
        return domain::StateBlobRef{}; // no captured state, and that is legal

    domain::StateBlobRef reference{};
    reference.digest = digestOf(data, size);
    reference.byteCount = static_cast<std::uint64_t>(size);

    const auto target = fileFor(reference.digest);
    if (target.existsAsFile() && static_cast<std::size_t>(target.getSize()) == size)
        return reference; // already stored, and the same bytes by construction

    const auto directory = target.getParentDirectory();
    if (const auto created = directory.createDirectory(); created.failed())
        return domain::fail(domain::ErrorCode::serialisationError,
                            "cannot create the blob directory: " + created.getErrorMessage().toStdString());

    // Written aside then moved: a crash mid-write must not leave a file whose
    // name claims a digest its content does not have.
    const auto temporary = target.getSiblingFile(target.getFileName() + ".part");
    temporary.deleteFile();

    {
        juce::FileOutputStream out{temporary};
        if (out.getStatus().failed())
            return domain::fail(domain::ErrorCode::serialisationError,
                                "cannot write the blob: " + temporary.getFullPathName().toStdString());

        if (!out.write(data, size))
            return domain::fail(domain::ErrorCode::serialisationError, "short write of a blob");
    }

    target.deleteFile();
    if (!temporary.moveFileTo(target))
    {
        temporary.deleteFile();
        return domain::fail(domain::ErrorCode::serialisationError,
                            "cannot commit the blob: " + target.getFullPathName().toStdString());
    }

    return reference;
}

domain::Result<juce::MemoryBlock> ContentStore::get(const domain::StateBlobRef& reference) const
{
    if (reference.isEmpty())
        return juce::MemoryBlock{};

    const auto source = fileFor(reference.digest);
    if (!source.existsAsFile())
        return domain::fail(domain::ErrorCode::notFound, "no stored blob for " + reference.digest);

    juce::MemoryBlock bytes;
    if (!source.loadFileAsData(bytes))
        return domain::fail(domain::ErrorCode::serialisationError,
                            "cannot read the blob " + reference.digest);

    if (bytes.getSize() != reference.byteCount)
        return domain::fail(domain::ErrorCode::serialisationError,
                            "stored blob has the wrong size: " + reference.digest);

    // The digest is the name of the file, so recomputing it is the only way to
    // notice a damaged blob before it reaches a plugin.
    if (digestOf(bytes.getData(), bytes.getSize()) != reference.digest)
        return domain::fail(domain::ErrorCode::serialisationError,
                            "stored blob is damaged: " + reference.digest);

    return bytes;
}

bool ContentStore::contains(const domain::StateBlobRef& reference) const
{
    return reference.isEmpty() || fileFor(reference.digest).existsAsFile();
}

} // namespace daw::engine
