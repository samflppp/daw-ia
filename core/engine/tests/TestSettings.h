#pragma once

#include <juce_core/juce_core.h>

namespace daw::testing
{

// Where this test process keeps the engine's settings and its plugin list
// (S22): a folder of its own, made the first time it is asked for and removed
// when the process ends. Before, every run shared %APPDATA%\daw_engine_tests,
// and a CLAP identity scanned from an old copy of the repository (C:\dawS9)
// was read back by every later run, on that machine only.
//
// ctest runs the cases as separate processes, side by side: the name is a
// fresh UUID, never "the next free number", which two processes could pick
// at once.
inline const juce::File& engineSettingsFolder()
{
    struct Folder
    {
        juce::File file{juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("daw_engine_tests")
                            .getChildFile("settings-" + juce::Uuid{}.toDashedString())};

        Folder() { file.createDirectory(); }
        ~Folder() { file.deleteRecursively(); }

        Folder(const Folder&) = delete;
        Folder& operator=(const Folder&) = delete;
        Folder(Folder&&) = delete;
        Folder& operator=(Folder&&) = delete;
    };

    static const Folder folder;
    return folder.file;
}

} // namespace daw::testing
