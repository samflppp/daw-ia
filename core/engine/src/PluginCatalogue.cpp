#include "daw/engine/PluginCatalogue.h"

#include "daw/engine/ClapPluginFormat.h"

namespace daw::engine
{
namespace
{

juce::String toJuce(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str(), static_cast<int>(text.size()));
}

} // namespace

PluginCatalogue::PluginCatalogue(tracktion::Engine& engine, juce::File listFile)
    : engine_{engine}
    , listFile_{std::move(listFile)}
{
}

juce::File PluginCatalogue::defaultListFile(const juce::String& applicationName)
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(applicationName)
        .getChildFile("plugins.xml");
}

bool PluginCatalogue::isHostedFormat(const juce::String& formatName)
{
    return formatName == "VST3" || formatName == ClapPluginFormat::formatName;
}

juce::File PluginCatalogue::deadMansPedalFile() const
{
    return listFile_.getSiblingFile("plugins-scanning.txt");
}

void PluginCatalogue::load()
{
    auto& list = engine_.getPluginManager().knownPluginList;

    // The file is the one juce::KnownPluginList writes itself, kept at a path
    // this project owns rather than inside Tracktion's property storage: a
    // scan is machine state, and it has to be readable and deletable by hand.
    if (const auto xml = juce::XmlDocument::parse(listFile_); xml != nullptr)
        list.recreateFromXml(*xml);

    // Whatever was being scanned when the scanner died is still named in the
    // dead man's pedal file. It goes to the blacklist before any plugin is
    // opened, so one hostile plugin cannot make every startup crash.
    juce::PluginDirectoryScanner::applyBlacklistingsFromDeadMansPedal(list, deadMansPedalFile());
}

domain::Result<void> PluginCatalogue::save() const
{
    const auto xml = engine_.getPluginManager().knownPluginList.createXml();
    if (xml == nullptr)
        return domain::fail(domain::ErrorCode::serialisationError, "the plugin list produced no XML");

    if (const auto created = listFile_.getParentDirectory().createDirectory(); created.failed())
        return domain::fail(domain::ErrorCode::serialisationError,
                            "cannot create the plugin list directory: " +
                                created.getErrorMessage().toStdString());

    if (!xml->writeTo(listFile_))
        return domain::fail(domain::ErrorCode::serialisationError,
                            "cannot write the plugin list: " + listFile_.getFullPathName().toStdString());

    return {};
}

PluginCatalogue::ScanReport PluginCatalogue::scan(bool rescanChangedFiles)
{
    auto& manager = engine_.getPluginManager();
    auto& list = manager.knownPluginList;

    ScanReport report{};

    if (rescanChangedFiles)
    {
        // A plugin that was updated on disk has to be described again. The
        // whole list is not thrown away for that: only the entries whose
        // binary no longer matches what was scanned.
        for (const auto& description : list.getTypes())
        {
            const juce::File file{description.fileOrIdentifier.upToFirstOccurrenceOf("|", false, false)};
            if (file.existsAsFile() || file.isDirectory())
            {
                if (description.lastFileModTime != file.getLastModificationTime())
                    list.removeType(description);
            }
        }
    }

    const auto pedal = deadMansPedalFile();
    pedal.getParentDirectory().createDirectory();

    for (auto* format : manager.pluginFormatManager.getFormats())
    {
        if (format == nullptr || !isHostedFormat(format->getName()))
            continue;

        juce::PluginDirectoryScanner scanner{
            list, *format, format->getDefaultLocationsToSearch(), true, pedal, true};

        juce::String pluginBeingScanned;
        const auto before = list.getNumTypes();
        const auto blacklistedBefore = list.getBlacklistedFiles().size();

        // dontRescanIfAlreadyInList: a known, unchanged plugin is never opened
        // again, which is what makes a startup cost nothing.
        while (scanner.scanNextFile(true, pluginBeingScanned))
            ++report.scanned;

        report.added += list.getNumTypes() - before;
        report.blacklisted += list.getBlacklistedFiles().size() - blacklistedBefore;
    }

    pedal.deleteFile();
    return report;
}

juce::Array<juce::PluginDescription> PluginCatalogue::descriptions() const
{
    juce::Array<juce::PluginDescription> hosted;
    for (const auto& description : engine_.getPluginManager().knownPluginList.getTypes())
    {
        if (isHostedFormat(description.pluginFormatName))
            hosted.add(description);
    }
    return hosted;
}

juce::StringArray PluginCatalogue::blacklist() const
{
    return engine_.getPluginManager().knownPluginList.getBlacklistedFiles();
}

domain::PluginRef PluginCatalogue::refFor(const juce::PluginDescription& description)
{
    domain::PluginRef ref{};
    ref.name = description.name.toStdString();

    if (description.pluginFormatName == ClapPluginFormat::formatName)
    {
        ref.format = std::string{domain::PluginRef::clapFormat};

        // A CLAP file can hold several plugins, so the format writes
        // "<path>|<clap id>" and the identity is the CLAP id: a plain string
        // the plugin author owns, identical on every machine.
        ref.identifier = description.fileOrIdentifier.fromLastOccurrenceOf("|", false, false).toStdString();
        if (ref.identifier.empty())
            ref.identifier = description.fileOrIdentifier.toStdString();
        return ref;
    }

    ref.format = std::string{domain::PluginRef::vst3Format};

    // Tracktion's own identifier string: format, name, manufacturer and unique
    // id, and no path. It is what ExternalPlugin uses to recognise a plugin
    // inside an Edit, so using anything else here would be a second truth.
    ref.identifier = tracktion::createIdentifierString(description).toStdString();
    return ref;
}

std::optional<juce::PluginDescription> PluginCatalogue::find(const domain::PluginRef& ref) const
{
    for (const auto& description : descriptions())
    {
        const auto candidate = refFor(description);
        if (candidate.format == ref.format && candidate.identifier == ref.identifier)
            return description;
    }

    // Second chance by name only: a plugin can change its unique id between
    // major versions, and refusing to load it at all would be worse than
    // loading the one the user clearly means.
    for (const auto& description : descriptions())
    {
        if (description.pluginFormatName == toJuce(ref.format) && description.name == toJuce(ref.name) &&
            !ref.name.empty())
            return description;
    }

    return std::nullopt;
}

} // namespace daw::engine
