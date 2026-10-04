#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/project/ProjectState.h"

#include <tracktion_engine/tracktion_engine.h>

#include <optional>

namespace daw::engine
{

// The list of plugins installed on this machine: VST3 and CLAP, Windows and
// Linux. Machine state, not project state — it is never journalled and never
// undone, so it lives outside ProjectState and outside the command bus.
//
// Three properties the rest of the project relies on:
//
//   persisted     the list is written to a file and reloaded at startup, so a
//                 launch costs no scan. Only what changed is scanned again.
//
//   crash-proof   scanning happens in a child process, and the file being
//                 scanned is written to a dead man's pedal file beforehand. A
//                 plugin that takes the scanner down with it is blacklisted on
//                 the next pass instead of being retried for ever.
//
//   translatable  a juce::PluginDescription becomes a domain::PluginRef and
//                 back. The domain names a plugin by format and stable
//                 identifier, never by path: a project has to survive a plugin
//                 moving on disk, and a move to another machine.
class PluginCatalogue
{
public:
    PluginCatalogue(tracktion::Engine& engine, juce::File listFile);

    struct ScanReport
    {
        int scanned{0};     // files the scanner opened
        int added{0};       // descriptions added to the list
        int blacklisted{0}; // files that failed, crash included
    };

    // Reads the persisted list, and blacklists whatever killed the scanner
    // last time before anything else is opened.
    void load();
    [[nodiscard]] domain::Result<void> save() const;

    // rescanChangedFiles compares the modification date of each known file and
    // forgets the entries whose binary changed, so a plugin update is seen
    // without a full rescan.
    ScanReport scan(bool rescanChangedFiles = true);

    [[nodiscard]] juce::Array<juce::PluginDescription> descriptions() const;
    [[nodiscard]] juce::StringArray blacklist() const;

    // Finds the installed plugin a project refers to. Absent means the plugin
    // is not on this machine: the caller reports it, it does not guess.
    [[nodiscard]] std::optional<juce::PluginDescription> find(const domain::PluginRef& ref) const;

    [[nodiscard]] static domain::PluginRef refFor(const juce::PluginDescription& description);

    // The formats this project hosts, by their juce::AudioPluginFormat name.
    [[nodiscard]] static bool isHostedFormat(const juce::String& formatName);

private:
    [[nodiscard]] juce::File deadMansPedalFile() const;

    tracktion::Engine& engine_;
    juce::File listFile_;
};

} // namespace daw::engine
