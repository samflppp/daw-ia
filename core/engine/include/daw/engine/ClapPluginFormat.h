#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace daw::engine
{

// CLAP hosting, written here because nothing hosts CLAP for us: neither the
// pinned JUCE nor the pinned Tracktion Engine contains a single line about the
// format. What exists is the CLAP SDK — headers only — and juce::PluginDescription
// plus juce::AudioPluginInstance, which is exactly the shape Tracktion's
// ExternalPlugin already knows how to drive.
//
// So the adapter is placed at the one point where it buys everything: a
// juce::AudioPluginFormat added to Tracktion's pluginFormatManager. From there,
// scanning, instantiation, the plugin chain, the parameter bridge and the
// commands do not know whether a plugin is VST3 or CLAP, and none of them has a
// branch on the format.
//
// What this host supports, and it is written down because a partial host that
// pretends to be complete is worse than a small one:
//
//   audio       any number of ports; the main input and output are used.
//   notes       MIDI 1 note on, note off, and the other three-byte messages,
//               through the CLAP MIDI dialect when the plugin takes it, and
//               through CLAP note events otherwise.
//   parameters  count, info, value, text, and the plugin's own changes and
//               gestures, reported to the host on the message thread.
//   state       clap.state, as an opaque blob — what the content-addressed
//               store holds.
//   gui         clap.gui embedded in a host window, win32 and x11.
//
// What it does not support: note expressions, polyphonic modulation, remote
// controls, surround layouts, the timer and fd extensions, and preset
// discovery. None of them is needed to load a user's plugin and hear it, and
// each would be a promise this project cannot yet keep.
class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    ClapPluginFormat();
    ~ClapPluginFormat() override;

    static constexpr const char* formatName = "CLAP";

    // The file extension is the same on every platform, and it is a plain
    // shared library: a DLL on Windows, an ELF object on Linux.
    static constexpr const char* fileExtension = ".clap";

    [[nodiscard]] juce::String getName() const override { return formatName; }

    void findAllTypesForFile(juce::OwnedArray<juce::PluginDescription>& results,
                             const juce::String& fileOrIdentifier) override;

    [[nodiscard]] bool fileMightContainThisPluginType(const juce::String& fileOrIdentifier) override;
    [[nodiscard]] juce::String getNameOfPluginFromIdentifier(const juce::String& fileOrIdentifier) override;
    [[nodiscard]] bool pluginNeedsRescanning(const juce::PluginDescription& description) override;
    [[nodiscard]] bool doesPluginStillExist(const juce::PluginDescription& description) override;
    [[nodiscard]] bool canScanForPlugins() const override { return true; }
    [[nodiscard]] bool isTrivialToScan() const override { return false; }

    [[nodiscard]] juce::StringArray
    searchPathsForPlugins(const juce::FileSearchPath& directoriesToSearch,
                          bool recursive,
                          bool allowPluginsWhichRequireAsynchronousInstantiation = false) override;

    [[nodiscard]] juce::FileSearchPath getDefaultLocationsToSearch() override;

    [[nodiscard]] bool
    requiresUnblockedMessageThreadDuringCreation(const juce::PluginDescription& description) const override;

    // "<path to the .clap file>|<clap plugin id>". A CLAP file can declare
    // several plugins, so the path alone is not an identity.
    [[nodiscard]] static juce::String makeFileOrIdentifier(const juce::File& file,
                                                           const juce::String& clapId);
    [[nodiscard]] static juce::File fileFromIdentifier(const juce::String& fileOrIdentifier);
    [[nodiscard]] static juce::String clapIdFromIdentifier(const juce::String& fileOrIdentifier);

protected:
    void createPluginInstance(const juce::PluginDescription& description,
                              double initialSampleRate,
                              int initialBufferSize,
                              PluginCreationCallback callback) override;
};

} // namespace daw::engine
