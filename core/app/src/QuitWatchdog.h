#pragma once

#include <juce_core/juce_core.h>

namespace daw::app
{

// Closing the application ends the process (S19).
//
// Once seen in S18: the window gone, the process still alive. Not reproduced
// since, in Release or Debug, playing, minimised. What is known is where a
// teardown can wait for ever without the application's own code being at
// fault: a thread joined without a limit (JUCE's vertical-blank thread waits
// for its next blank to see that it must stop, and a display that gives none
// never lets it), an audio driver or a plugin that does not let go.
//
// So the teardown is split in two. Up to the moment the project, what was
// learned and the settings are on disk, nothing is hurried: that is the work a
// person must not lose. After it, what is left is giving back memory, threads
// and devices, and a deadline is armed: past it, the step the teardown was
// stuck in is written to the log, and the process ends there. The next
// occurrence says where it was, instead of a process killed by hand.
class QuitWatchdog final
{
public:
    // What the teardown is doing now, for the log if it never comes back.
    // A literal: it is read from another thread.
    static void step(const char* name) noexcept;

    // From now on the process ends within deadlineMs, whatever is left.
    static void arm(const juce::File& logFile, int deadlineMs);

    // shutdown() came back: said once in the log, with its length. The
    // deadline still holds for what JUCE and the statics do after it.
    static void done();
};

} // namespace daw::app
