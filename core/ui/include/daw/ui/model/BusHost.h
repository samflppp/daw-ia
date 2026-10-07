#pragma once

#include "daw/domain/buses/Shared.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/FluxHost.h"

#include <juce_events/juce_events.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace daw::ui
{

// The smart buses (S24), as their window is allowed to see them: the
// proposals read from the project, one tried on a copy (the before and the
// after rendered and measured), listened to at equal loudness, seen in the
// flux, kept in one group or refused. The renders, the files and the bus stop
// at the application.
class BusHost : public juce::ChangeBroadcaster
{
public:
    enum class Stage
    {
        idle,   // proposals, none tried
        trying, // the renders: progress() moves
        tried,  // one tried: result() says what it does
        failed  // why in status()
    };

    // What the dry run measured.
    struct Tried
    {
        double lufsBefore{0.0};
        double lufsAfter{0.0};
        double differenceDb{0.0};    // the after against the before, sample by sample: -240 the same
        std::optional<double> dryDb; // a send's effect: the dry sound it lets through
        std::vector<std::string> said;
    };

    BusHost() = default;
    ~BusHost() override = default;
    BusHost(const BusHost&) = delete;
    BusHost& operator=(const BusHost&) = delete;
    BusHost(BusHost&&) = delete;
    BusHost& operator=(BusHost&&) = delete;

    [[nodiscard]] virtual Stage stage() const = 0;
    [[nodiscard]] virtual double progress() const = 0;
    [[nodiscard]] virtual std::string status() const = 0;

    // Reads the project again.
    virtual void propose() = 0;
    [[nodiscard]] virtual const std::vector<domain::buses::Shared>& proposals() const = 0;

    virtual void tryOut(std::size_t proposal) = 0;
    [[nodiscard]] virtual std::optional<std::size_t> tried() const = 0;
    [[nodiscard]] virtual const Tried* result() const = 0;

    // The project as the tried proposal would leave it, and the sound of its
    // places on the copy, as MixHost gives them for a mix.
    [[nodiscard]] virtual const domain::ProjectState* proposedState() const = 0;
    [[nodiscard]] virtual std::vector<float> proposedSound(const FluxHost::Place& place) const = 0;

    // Before or after, at equal loudness; silence.
    virtual void listen(bool after) = 0;
    virtual void stopListening() = 0;

    virtual bool keep() = 0;
    virtual void refuse() = 0;
};

} // namespace daw::ui
