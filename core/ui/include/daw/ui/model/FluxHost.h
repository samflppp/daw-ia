#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daw::ui
{

// The sound at each place of the audio flux (S24), as the flux window is
// allowed to see it: places to arm, and their samples read by position. The
// taps and the Edit stop at the application.
//
// A place is a strip — a TrackId's text, the master's included — and a slot:
// « source » (a channel's instrument and recordings, a bus's or the master's
// sum), « fader » (after the fader), or the id of the effect it follows.
class FluxHost
{
public:
    struct Place
    {
        std::string strip;
        std::string slot;

        friend bool operator==(const Place&, const Place&) = default;
    };

    static constexpr const char* sourceSlot = "source";
    static constexpr const char* faderSlot = "fader";

    FluxHost() = default;
    virtual ~FluxHost() = default;
    FluxHost(const FluxHost&) = delete;
    FluxHost& operator=(const FluxHost&) = delete;
    FluxHost(FluxHost&&) = delete;
    FluxHost& operator=(FluxHost&&) = delete;

    // These places write what passes, every other one stops: the window arms
    // what it shows, and nothing when it is hidden.
    virtual void arm(const std::vector<Place>& places) = 0;

    // The last position written among the armed places (-1 before any), the
    // rate of the positions, and the samples of one place from `from`.
    [[nodiscard]] virtual std::int64_t latest() const = 0;
    [[nodiscard]] virtual double sampleRate() const = 0;
    virtual void read(const Place& place, std::int64_t from, int count, float* out) const = 0;

    // Listening alone at one place, mono, in place of the mix — or no longer.
    // A state of the screen: nothing of it is written in the project.
    virtual void listen(const std::optional<Place>& place) = 0;
    [[nodiscard]] virtual std::optional<Place> listening() const = 0;
};

} // namespace daw::ui
