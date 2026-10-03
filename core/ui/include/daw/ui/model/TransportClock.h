#pragma once

#include <string>

namespace daw::ui
{

// Where the playhead actually is.
//
// It is not in ProjectState and it cannot be: the domain's TransportState
// remembers the position a command set, while the playhead moves on its own,
// sixty times a second, inside the engine. Asking the domain would show a
// readout frozen at the last click.
//
// It is an interface so that core/ui keeps knowing nothing of Tracktion. The
// application implements it over the Edit; a test can implement it with two
// numbers.
class TransportClock
{
public:
    TransportClock() = default;
    virtual ~TransportClock() = default;

    TransportClock(const TransportClock&) = delete;
    TransportClock& operator=(const TransportClock&) = delete;
    TransportClock(TransportClock&&) = delete;
    TransportClock& operator=(TransportClock&&) = delete;

    [[nodiscard]] virtual double positionBeats() const = 0;
    [[nodiscard]] virtual bool isPlaying() const = 0;

    // Where to draw the playhead at this image (S18 bis). The engine moves its
    // position once per audio block, some twenty milliseconds, and a display
    // shows an image every seven to seventeen: read as it is, the playhead
    // stands still on one image in two and jumps on the next. An
    // implementation may carry it forward between two blocks. Drawing only:
    // what an edit lands on is positionBeats().
    [[nodiscard]] virtual double displayBeats() const { return positionBeats(); }

    // What the screen says about the sound card under the position (S21):
    // « sortie perdue » for as long as no card is open, « sortie : <name> » for
    // a few seconds after the song moved to another one. Empty otherwise.
    [[nodiscard]] virtual std::string outputNotice() const { return {}; }

    // Whether the notice says the sound is gone, rather than where it went.
    [[nodiscard]] virtual bool outputLost() const { return false; }
};

} // namespace daw::ui
