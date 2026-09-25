#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace daw::engine
{

// Renders the whole Edit into a WAV file, the way it plays.
//
// Not through Renderer::renderToFile(Edit&, File, bool): that one is the freeze
// path, and it unmutes every track it renders before it starts — a muted track
// is heard in it. The meters found it at S11, the first time anything rendered
// a mute. This one mutes what is muted, keeps the master plugins, and writes
// 32-bit samples, so a level past full scale is kept rather than clipped.
//
// Run on the calling thread, which has to be the message thread.
[[nodiscard]] bool renderAsPlayed(tracktion::Edit& edit, const juce::File& file);

} // namespace daw::engine
