#pragma once

#include "daw/engine/FluxTap.h"

#include <tracktion_engine/tracktion_engine.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace daw::engine
{

// The taps of the audio flux (S24), read by place: what the flux window arms
// and reads, on the message thread. A place is a strip — a domain TrackId's
// text, the master's included — and a slot: « source », « fader », or the
// domain id of the effect it follows.
//
// A place can be two taps: a channel's instrument and its recordings play on
// two tracks of the Edit (the companion), and what the channel makes at that
// place is the two added, sample for sample, by position.
class FluxTaps
{
public:
    struct Place
    {
        std::string strip;
        std::string slot;
    };

    explicit FluxTaps(tracktion::Edit& edit);

    // These places armed, every other tap not: the window arms what it shows.
    void arm(const std::vector<Place>& places);

    // The last position written among the armed taps, -1 before any.
    [[nodiscard]] std::int64_t latest() const;
    [[nodiscard]] double sampleRate() const;

    // The samples of one place from position `from`, the taps there added;
    // zeros where no tap is, or where its ring no longer holds them.
    void read(const Place& place, std::int64_t from, int count, float* out) const;

    // Listens alone at one place, in place of the mix, or no longer. Played
    // live only, unless `alsoRendering` (a test).
    void listen(const std::optional<Place>& place, bool alsoRendering = false);
    [[nodiscard]] const std::optional<Place>& listening() const noexcept { return listening_; }

    // The taps of one place, for a test or the cost measure.
    [[nodiscard]] std::vector<FluxTapPlugin*> tapsAt(const Place& place) const;
    [[nodiscard]] std::vector<FluxTapPlugin*> all() const;

private:
    [[nodiscard]] static juce::String stripOf(const std::string& strip);

    tracktion::Edit& edit_;
    std::optional<Place> listening_;
};

} // namespace daw::engine
