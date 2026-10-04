#pragma once

#include "daw/domain/direction/Direction.h"

#include <juce_events/juce_events.h>

#include <optional>
#include <string>

namespace daw::ui
{

// What a panel may ask of the direction by references (S22).
//
// Reading a reference separates it into stems and measures them, in another
// process and on a thread of its own, for a minute or more: not a panel's
// job. What the direction says is in the project (ProjectState::direction),
// which a panel reads like the rest; every change is written by direction.set,
// one Ctrl+Z each.
class DirectionHost : public juce::ChangeBroadcaster
{
public:
    enum class Stage
    {
        idle,
        reading, // a reference is being separated and measured
        failed   // why in status()
    };

    DirectionHost() = default;
    ~DirectionHost() override = default;
    DirectionHost(const DirectionHost&) = delete;
    DirectionHost& operator=(const DirectionHost&) = delete;
    DirectionHost(DirectionHost&&) = delete;
    DirectionHost& operator=(DirectionHost&&) = delete;

    [[nodiscard]] virtual Stage stage() const = 0;
    [[nodiscard]] virtual double progress() const = 0;    // 0..1
    [[nodiscard]] virtual std::string status() const = 0; // French, one line

    // Reads an audio file and adds it to the project's direction, weight 1.
    // Returns at once; the direction changes when the reading ends.
    virtual void addReference(const std::string& path) = 0;
    virtual void cancel() = 0;

    // Each a direction.set of the direction as it now stands, changed.
    virtual void removeReference(const std::string& digest) = 0;
    virtual void setWeight(const std::string& digest, double weight) = 0;
    virtual void setAmount(double amount) = 0;
    virtual void correctTempo(std::optional<double> bpm) = 0;
    virtual void correctKey(std::optional<domain::generation::Key> key) = 0;
    virtual void clear() = 0; // no reference, no correction: the project as without one
};

} // namespace daw::ui
