#include "daw/engine/ClapPluginFormat.h"

#include "clap/ClapPluginInstance.h"

namespace daw::engine
{
namespace
{

// A .clap file is a plain shared library on the two platforms this project
// targets, so scanning it means loading it and asking its factory.
juce::Array<juce::File> clapFilesIn(const juce::File& directory, bool recursive)
{
    juce::Array<juce::File> found;
    if (!directory.isDirectory())
        return found;

    directory.findChildFiles(
        found, juce::File::findFiles, recursive, juce::String{"*"} + ClapPluginFormat::fileExtension);
    return found;
}

} // namespace

ClapPluginFormat::ClapPluginFormat() = default;
ClapPluginFormat::~ClapPluginFormat() = default;

juce::String ClapPluginFormat::makeFileOrIdentifier(const juce::File& file, const juce::String& clapId)
{
    return file.getFullPathName() + "|" + clapId;
}

juce::File ClapPluginFormat::fileFromIdentifier(const juce::String& fileOrIdentifier)
{
    return juce::File{fileOrIdentifier.upToLastOccurrenceOf("|", false, false).isEmpty()
                          ? fileOrIdentifier
                          : fileOrIdentifier.upToLastOccurrenceOf("|", false, false)};
}

juce::String ClapPluginFormat::clapIdFromIdentifier(const juce::String& fileOrIdentifier)
{
    if (!fileOrIdentifier.containsChar('|'))
        return {};

    return fileOrIdentifier.fromLastOccurrenceOf("|", false, false);
}

void ClapPluginFormat::findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                                           const juce::String& fileOrIdentifier)
{
    const auto file = fileFromIdentifier(fileOrIdentifier);
    const auto wantedId = clapIdFromIdentifier(fileOrIdentifier);

    if (!file.existsAsFile() && !file.isDirectory())
        return;

    auto library = clap_host::EntryLibrary::open(file);
    if (library == nullptr)
        return;

    const auto* factory = library->factory();
    const auto count = factory->get_plugin_count(factory);

    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto* descriptor = factory->get_plugin_descriptor(factory, index);
        if (descriptor == nullptr || descriptor->id == nullptr)
            continue;

        const auto clapId = juce::String::fromUTF8(descriptor->id);
        if (wantedId.isNotEmpty() && wantedId != clapId)
            continue;

        // The description is built by instantiating the plugin: the port and
        // parameter counts a project needs are only knowable from an instance,
        // and a plugin that cannot be instantiated has no business in the list.
        juce::String errorMessage;
        auto instance = clap_host::PluginInstance::create(file, clapId, 44100.0, 512, errorMessage);
        if (instance == nullptr)
            continue;

        auto description = std::make_unique<juce::PluginDescription>();
        instance->fillInPluginDescription(*description);
        results.add(description.release());
    }
}

bool ClapPluginFormat::fileMightContainThisPluginType(const juce::String& fileOrIdentifier)
{
    const auto file = fileFromIdentifier(fileOrIdentifier);
    return file.hasFileExtension(fileExtension);
}

juce::String ClapPluginFormat::getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier)
{
    const auto clapId = clapIdFromIdentifier(fileOrIdentifier);
    if (clapId.isNotEmpty())
        return clapId;

    return fileFromIdentifier(fileOrIdentifier).getFileNameWithoutExtension();
}

bool ClapPluginFormat::pluginNeedsRescanning(const juce::PluginDescription& description)
{
    const auto file = fileFromIdentifier(description.fileOrIdentifier);
    return file.getLastModificationTime() != description.lastFileModTime;
}

bool ClapPluginFormat::doesPluginStillExist(const juce::PluginDescription& description)
{
    const auto file = fileFromIdentifier(description.fileOrIdentifier);
    return file.existsAsFile() || file.isDirectory();
}

juce::StringArray
ClapPluginFormat::searchPathsForPlugins(const juce::FileSearchPath& directoriesToSearch,
                                        bool recursive,
                                        bool allowPluginsWhichRequireAsynchronousInstantiation)
{
    juce::ignoreUnused(allowPluginsWhichRequireAsynchronousInstantiation);

    juce::StringArray identifiers;
    for (int index = 0; index < directoriesToSearch.getNumPaths(); ++index)
    {
        for (const auto& file : clapFilesIn(directoriesToSearch[index], recursive))
            identifiers.add(file.getFullPathName());
    }

    identifiers.removeDuplicates(true);
    return identifiers;
}

juce::FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    // The locations the CLAP specification names, and only those: a DAW that
    // invents its own would find plugins no other host sees.
    juce::FileSearchPath paths;

#if JUCE_WINDOWS
    if (const auto common = juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory);
        common != juce::File{})
        paths.add(common.getSiblingFile("Common Files").getChildFile("CLAP"));

    paths.add(juce::File{"C:\\Program Files\\Common Files\\CLAP"});
    paths.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("Programs")
                  .getChildFile("Common")
                  .getChildFile("CLAP"));
#elif JUCE_MAC
    paths.add(juce::File{"/Library/Audio/Plug-Ins/CLAP"});
    paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                  .getChildFile("Library/Audio/Plug-Ins/CLAP"));
#else
    paths.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".clap"));
    paths.add(juce::File{"/usr/lib/clap"});
    paths.add(juce::File{"/usr/local/lib/clap"});
#endif

    // CLAP_PATH is part of the specification: it is how a user points a host at
    // plugins installed somewhere else.
    if (const auto extra = juce::SystemStats::getEnvironmentVariable("CLAP_PATH", {}); extra.isNotEmpty())
    {
#if JUCE_WINDOWS
        const auto separator = ";";
#else
        const auto separator = ":";
#endif
        for (const auto& entry : juce::StringArray::fromTokens(extra, separator, {}))
        {
            if (entry.isNotEmpty())
                paths.add(juce::File{entry});
        }
    }

    return paths;
}

bool ClapPluginFormat::requiresUnblockedMessageThreadDuringCreation(
    const juce::PluginDescription& description) const
{
    juce::ignoreUnused(description);
    return false; // creation is synchronous: the factory answers or it does not
}

void ClapPluginFormat::createPluginInstance(const juce::PluginDescription& description,
                                            double initialSampleRate,
                                            int initialBufferSize,
                                            PluginCreationCallback callback)
{
    const auto file = fileFromIdentifier(description.fileOrIdentifier);
    const auto clapId = clapIdFromIdentifier(description.fileOrIdentifier);

    juce::String errorMessage;
    auto instance =
        clap_host::PluginInstance::create(file, clapId, initialSampleRate, initialBufferSize, errorMessage);

    if (instance == nullptr && errorMessage.isEmpty())
        errorMessage = "cannot load the CLAP plugin " + description.name;

    callback(std::move(instance), errorMessage);
}

} // namespace daw::engine
