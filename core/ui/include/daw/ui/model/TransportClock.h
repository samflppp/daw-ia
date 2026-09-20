#pragma once

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
};

} // namespace daw::ui
