#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace daw::engine
{

// Carries out a transport command on Tracktion's TransportControl.
//
// It used to mirror the domain's TransportState and skip any call that changed
// nothing. That was wrong in one direction nobody had looked at: Tracktion
// stops on its own when playback runs off the end of the material, and nothing
// tells the domain. The mirror then said "playing" while the engine was
// silent, so the next transport.play was not a change, and the button did
// nothing at all. The same held for the playhead: transport.set_position(0) on
// a domain that already held 0 never reached the engine, so the rewind button
// was dead.
//
// So there is no mirror any more. Each verb is called when its own command
// arrives, and it always reaches the engine. A projection of the project never
// touches the transport: a fader frame cannot restart playback because nothing
// here is called on a fader frame.
class TransportController
{
public:
    explicit TransportController(tracktion::Edit& edit) noexcept;

    void play();
    void stop();

    // In beats, converted through the tempo sequence: the domain speaks in
    // beats and the engine in seconds, and a tempo change moves the second
    // without moving the beat.
    void setPosition(double positionBeats);

    // Enables or disables the loop, and sets its range. In beats, converted
    // through the tempo sequence like every other position.
    void setLoop(bool looping, double startBeats, double endBeats);

private:
    tracktion::Edit& edit_;
};

} // namespace daw::engine
