#pragma once

#include <juce_core/juce_core.h>

#include <optional>
#include <vector>

namespace daw::app::native
{

// What Windows says of its audio endpoints (S25), read through its own
// interfaces (MMDevice, the device topology), which JUCE does not expose:
// JUCE gives an endpoint's name, not what it is plugged into.
//
// A Bluetooth headset's microphone is the trap of the push-to-talk: opening
// it makes Windows leave the headset's music profile for its hands-free one,
// and the song comes out of the same headset in telephone quality. Whether
// an endpoint is Bluetooth is read from the device it is connected to (a
// Bluetooth audio driver's identifier starts with BTHENUM or BTHHFENUM), not
// guessed from its name.
struct Endpoint
{
    juce::String name; // the endpoint's friendly name, as JUCE's WASAPI lists it
    bool bluetooth{false};
    bool isDefault{false}; // Windows' default for this direction (console role)
    bool active{false};
    // The shared-mode mix format, « 48000 Hz, 2 canaux »; empty when not read.
    juce::String format;
};

// The capture endpoints that are present. Empty elsewhere than Windows.
[[nodiscard]] std::vector<Endpoint> captureEndpoints();

// The render endpoint of that name, as it is now; nothing when absent.
[[nodiscard]] std::optional<Endpoint> renderEndpoint(const juce::String& name);

} // namespace daw::app::native
