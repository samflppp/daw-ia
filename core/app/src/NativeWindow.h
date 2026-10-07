#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::app::native
{

// What the system says is under a point of the screen, asked of the window's
// own procedure the way Windows asks it (WM_NCHITTEST): "caption", "minimise",
// "maximise", "close", "edge" or "client". "unsupported" on another system.
//
// For the verification: the title bar is a caption because Windows is told so,
// and only asking Windows proves it (S18 bis).
[[nodiscard]] juce::String hitTest(juce::ComponentPeer& peer, juce::Point<int> screen);

// A double-click on the caption, delivered as Windows delivers it. False on
// another system.
bool doubleClickCaption(juce::ComponentPeer& peer, juce::Point<int> screen);

// What Windows says of a window and of the screen, for a check that does not
// see a window shown (S25): the window's own state (« réduite », « cachée »,
// « affichée ») and the window in the foreground, its title and whether it is
// this process's. "unsupported" on another system.
[[nodiscard]] juce::String windowState(juce::ComponentPeer& peer);
[[nodiscard]] juce::String foreground();

} // namespace daw::app::native
