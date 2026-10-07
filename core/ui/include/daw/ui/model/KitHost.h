#pragma once

#include "daw/domain/kit/Choice.h"

#include <juce_events/juce_events.h>

#include <cstddef>
#include <string>

namespace daw::ui
{

// The kit, « assembler » version (S24), as its window is allowed to see it:
// the index of the person's samples, built on the machine off the message
// thread; the kit chosen from it by rules; listening before laying it down;
// laying it down in one group. The files, the threads and the bus stop at
// the application.
class KitHost : public juce::ChangeBroadcaster
{
public:
    enum class Stage
    {
        idle,     // no index yet
        indexing, // progress() moves; cancel() stops it
        ready,    // an index; choose() gives a kit
        failed    // why in status()
    };

    KitHost() = default;
    ~KitHost() override = default;
    KitHost(const KitHost&) = delete;
    KitHost& operator=(const KitHost&) = delete;
    KitHost(KitHost&&) = delete;
    KitHost& operator=(KitHost&&) = delete;

    [[nodiscard]] virtual Stage stage() const = 0;
    [[nodiscard]] virtual double progress() const = 0;    // 0..1 while indexing
    [[nodiscard]] virtual std::string status() const = 0; // French, one line

    // Measures what changed in the browser's folders, reuses the rest.
    virtual void index() = 0;
    virtual void cancel() = 0;

    // The samples whose role is known, as the kit chooses from them.
    [[nodiscard]] virtual std::size_t librarySize() const = 0;

    // The axes where the direction of the project puts them: its colour gives
    // the brightness, its width the ampleness, in proportion to its amount.
    [[nodiscard]] virtual domain::kit::Axes directionAxes() const = 0;

    // Chooses: the project's key for the 808, these axes for the rest.
    virtual void choose(const domain::kit::Axes& axes) = 0;
    [[nodiscard]] virtual const domain::kit::Kit* kit() const = 0;

    // Two bars of the kit, mixed in memory at the project's tempo and played
    // through the browser's preview; one element alone; silence. Nothing is
    // written in the project.
    virtual void preview() = 0;
    virtual void listenTo(std::size_t pick) = 0;
    virtual void stop() = 0;

    // Lays the kit down: a channel per element on its sample, one group, one
    // Ctrl+Z. Says what was missing.
    virtual bool pose() = 0;
};

} // namespace daw::ui
