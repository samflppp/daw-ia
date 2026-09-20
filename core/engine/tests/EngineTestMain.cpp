// Own main(): a Tracktion Engine needs JUCE's message manager and its shared
// singletons alive for the whole run, so the initialiser has to outlive every
// test case.
//
// And this binary has a second job. With --child it is the process a test
// launched to write a project and then die: persistence is measured by closing
// a process and opening another one, never by calling a loader twice.
#define DOCTEST_CONFIG_IMPLEMENT

#include "PluginPersistenceScenario.h"

#include <juce_events/juce_events.h>

#include <doctest/doctest.h>

int main(int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc >= 5 && juce::String{argv[1]} == "--child" && juce::String{argv[2]} == "write-plugin-project")
        return daw::testing::writePluginProject(juce::File{juce::String{argv[3]}},
                                                juce::File{juce::String{argv[4]}});

    doctest::Context context;
    context.applyCommandLine(argc, argv);

    const auto result = context.run();
    return context.shouldExit() ? result : result;
}
