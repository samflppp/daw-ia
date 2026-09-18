// Own main(): a Tracktion Engine needs JUCE's message manager and its shared
// singletons alive for the whole run, so the initialiser has to outlive every
// test case.
#define DOCTEST_CONFIG_IMPLEMENT

#include <juce_events/juce_events.h>

#include <doctest/doctest.h>

int main(int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInitialiser;

    doctest::Context context;
    context.applyCommandLine(argc, argv);

    const auto result = context.run();
    return context.shouldExit() ? result : result;
}
