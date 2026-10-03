#pragma once

#include <cstdint>

namespace daw::domain
{

// What part of the project a command changes, as far as a reader that redraws
// it needs to know (S19). Declared by the command, never guessed from its
// name: until S19 the playlist read the prefix "note." of the type, and a
// command named one way and doing another would have left a screen late
// without an error.
//
// The default is `anything`: a command that says nothing costs a repaint too
// many, never one too few. Only a command that changes the notes of a row and
// nothing else declares `notes`, and a test applies each of those and checks
// that nothing else moved.
//
// `mix` (S20): a strip of the mixer and nothing else — its fader, pan, mute,
// solo, sends, output, chain of plugins. No content, no structure: no track,
// pattern or note appears or leaves. The mixer repaints on it, and the mix
// measurement knows that what a track plays has not moved.
enum class Reach : std::uint8_t
{
    anything,
    notes,
    mix,
};

} // namespace daw::domain
