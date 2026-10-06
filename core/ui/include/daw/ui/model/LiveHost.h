#pragma once

#include <string>

namespace daw::ui
{

// Playing live (S23), as a panel is allowed to see it: the mode of the
// computer's keyboard, its octave and velocity, and which track the keys
// play. The router, the input threads and the engine stop at the
// application. A screen setting, never the project's: none of it is
// journalled, undone or saved with the song.
class LiveHost
{
public:
    LiveHost() = default;
    virtual ~LiveHost() = default;
    LiveHost(const LiveHost&) = delete;
    LiveHost& operator=(const LiveHost&) = delete;
    LiveHost(LiveHost&&) = delete;
    LiveHost& operator=(LiveHost&&) = delete;

    // The computer's keyboard plays notes (the transport's button, Ctrl+T).
    [[nodiscard]] virtual bool keyboardPlaying() const = 0;
    virtual void setKeyboardPlaying(bool playing) = 0;

    // Whether this machine can read the keys by their place (Raw Input, on
    // Windows). Without it the mode cannot be turned on, and the button says
    // why.
    [[nodiscard]] virtual bool keyboardAvailable() const = 0;

    // The octave of the bottom row (4: it starts on do3, MIDI 60) and the
    // fixed velocity of the keys, 1 to 127.
    [[nodiscard]] virtual int octave() const = 0;
    [[nodiscard]] virtual int velocity() const = 0;
    virtual void setVelocity(int velocity) = 0;

    // The name of the track the keys play, empty when none is chosen.
    [[nodiscard]] virtual std::string targetName() const = 0;
};

} // namespace daw::ui
