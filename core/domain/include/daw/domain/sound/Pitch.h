#pragma once

namespace daw::domain::sound
{

// The fundamental of a monophonic window, in Hz, or 0 when nothing periodic is
// there: YIN (de Cheveigné and Kawahara, 2002), the plain version.
//
// First here to prove on a render that a note sounds at the pitch it is
// written at; in the domain since S24, where the kit reads a sample's pitch. Under about 100 Hz it can lock
// onto twice the period of a rich wave, which changes the octave and never the pitch class -- the class is
// what a key judges, and what the callers compare.
[[nodiscard]] double
fundamentalOf(const float* samples, int count, double sampleRate, double lowest, double highest);

// The nearest MIDI pitch of a frequency.
[[nodiscard]] int midiPitchOf(double hertz);

} // namespace daw::domain::sound
