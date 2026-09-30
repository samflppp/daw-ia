#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::app::display
{

// Fluide or léger: how the screen moves on this machine (S18 bis).
//
// A setting of the machine, like where its windows are, and for the reason the
// selection is not in the project (S9): the same project opened on a strong
// machine and a weak one must look the same, and move as each can. It lives in
// the application's settings, in AppData; never in ProjectState, never in the
// history.
//
//   fluide  the frames follow the display (FrameTicker), Direct2D draws
//   léger   thirty frames a second, and JUCE's software renderer instead of
//           Direct2D, for a graphics driver that is the problem
//
// Applied at once, to every window, without a restart.

[[nodiscard]] bool isLight(const juce::PropertySet* settings);
void setLight(juce::PropertySet* settings, bool light);

// The pace of every FrameTicker, and the renderer of every window open.
void apply(bool light);

// For a window opened later: its renderer, as the setting says.
void applyTo(juce::ComponentPeer& peer);

// The name of the renderer a window draws with, for the verification.
[[nodiscard]] juce::String rendererOf(juce::ComponentPeer& peer);

} // namespace daw::app::display
