#pragma once

#include "daw/domain/Ids.h"

#include <juce_events/juce_events.h>

#include <string>

namespace daw::ui
{

// What a panel is allowed to know of the stem separator (S22).
//
// A separation runs in another process for minutes; installing the models
// the first time, the cache, laying the stems: none of that belongs in a
// panel. The panel asks to separate an audio clip, shows a stage and a
// progress, and can cancel — cancel() returns at once. The same shape as
// MixHost, for the same reason.
class StemHost : public juce::ChangeBroadcaster
{
public:
    enum class Stage
    {
        idle,
        installing, // the models' environment, the first time only
        separating, // progress() moves
        failed      // why in status()
    };

    // best: the best model this build knows, slow; fast: a few minutes for a
    // song on a laptop.
    enum class Quality
    {
        best,
        fast
    };

    StemHost() = default;
    ~StemHost() override = default;
    StemHost(const StemHost&) = delete;
    StemHost& operator=(const StemHost&) = delete;
    StemHost(StemHost&&) = delete;
    StemHost& operator=(StemHost&&) = delete;

    [[nodiscard]] virtual Stage stage() const = 0;
    [[nodiscard]] virtual double progress() const = 0;    // 0..1
    [[nodiscard]] virtual std::string status() const = 0; // French, one line

    // Separates an audio clip of the project: its stems take its place, on
    // tracks of their own, in one group of the history. Returns at once.
    virtual void separate(domain::AudioClipId clip, Quality quality) = 0;

    // Ends the separation in flight. Returns at once; nothing is written.
    virtual void cancel() = 0;
};

} // namespace daw::ui
