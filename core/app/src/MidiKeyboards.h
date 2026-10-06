#pragma once

#include "daw/domain/live/Router.h"

#include <juce_events/juce_events.h>
#include <tracktion_engine/tracktion_engine.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace daw::app
{

// The MIDI keyboards (S23): every input of the machine plays the chosen track
// — the simplest thing that works for a beginner with one keyboard, nothing
// to choose. A setting of the machine, never of the project.
//
// The inputs are Tracktion's own: its device manager opens them (Windows does
// not let two clients open one port) and looks for new ones every second. Each
// gets a listener here, which hands what it plays to the router on the MIDI
// thread, stamped with the input clock. An input unplugged releases what it
// held, and the screen says so; plugged back, it plays again.
//
// Notes, velocity, sustain, pitch bend and modulation are played; the take
// keeps the notes, their velocity and the pedal (as lengths). Aftertouch and
// the other controllers reach the instrument too.
class MidiKeyboards final : private juce::ChangeListener
{
public:
    MidiKeyboards(tracktion::Engine& engine, domain::live::Router& router);
    ~MidiKeyboards() override;

    MidiKeyboards(const MidiKeyboards&) = delete;
    MidiKeyboards& operator=(const MidiKeyboards&) = delete;
    MidiKeyboards(MidiKeyboards&&) = delete;
    MidiKeyboards& operator=(MidiKeyboards&&) = delete;

    // The names of the inputs being listened to.
    [[nodiscard]] std::vector<std::string> names() const;

    // Said once per change: « clavier MIDI branché : Arturia », « … débranché ».
    std::function<void(const std::string& what)> onChanged;

    // Looks at the inputs now. Called on every change of the devices.
    void follow();

private:
    class Input;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    tracktion::Engine& engine_;
    domain::live::Router& router_;
    std::vector<std::unique_ptr<Input>> inputs_;
};

} // namespace daw::app
