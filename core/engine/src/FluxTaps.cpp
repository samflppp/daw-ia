#include "daw/engine/FluxTaps.h"

#include "daw/domain/project/ProjectState.h"
#include "daw/engine/MeterTap.h"

#include <algorithm>

namespace daw::engine
{

FluxTaps::FluxTaps(tracktion::Edit& edit)
    : edit_{edit}
{
}

juce::String FluxTaps::stripOf(const std::string& strip)
{
    // The master's taps carry the meter's name for it, not its TrackId.
    if (strip == domain::ProjectState::masterTrackId().toString())
        return MeterTapPlugin::masterStrip;
    return juce::String::fromUTF8(strip.c_str());
}

std::vector<FluxTapPlugin*> FluxTaps::all() const
{
    std::vector<FluxTapPlugin*> taps;
    for (auto* track : tracktion::getAudioTracks(edit_))
        for (auto* tap : track->pluginList.getPluginsOfType<FluxTapPlugin>())
            taps.push_back(tap);
    for (auto* tap : edit_.getMasterPluginList().getPluginsOfType<FluxTapPlugin>())
        taps.push_back(tap);
    return taps;
}

std::vector<FluxTapPlugin*> FluxTaps::tapsAt(const Place& place) const
{
    const auto strip = stripOf(place.strip);
    const auto slot = juce::String::fromUTF8(place.slot.c_str());
    auto taps = all();
    taps.erase(std::remove_if(taps.begin(),
                              taps.end(),
                              [&](const FluxTapPlugin* tap)
                              {
                                  return tap == nullptr || tap->strip() != strip || tap->slot() != slot ||
                                         (place.companion && tap->companion() != *place.companion);
                              }),
               taps.end());
    return taps;
}

void FluxTaps::arm(const std::vector<Place>& places)
{
    std::vector<FluxTapPlugin*> wanted;
    for (const auto& place : places)
        for (auto* tap : tapsAt(place))
            wanted.push_back(tap);
    for (auto* tap : all())
        tap->arm(std::find(wanted.begin(), wanted.end(), tap) != wanted.end());
}

void FluxTaps::listen(const std::optional<Place>& place, bool alsoRendering)
{
    auto& monitor = FluxMonitor::instance();
    for (auto* tap : all())
        tap->monitor(-1);
    listening_ = place;
    if (!place)
    {
        monitor.playOutThrough(nullptr);
        return;
    }

    // The master's way out plays it; the place's taps write it, the
    // channel's track and its companion each into a ring of its own.
    const FluxTapPlugin* out = nullptr;
    for (auto* tap : edit_.getMasterPluginList().getPluginsOfType<FluxTapPlugin>())
        if (tap->slot() == FluxTapPlugin::faderSlot)
            out = tap;
    monitor.playOutThrough(out, alsoRendering);
    for (auto* tap : tapsAt(*place))
        tap->monitor(tap->companion() ? 1 : 0);
}

std::int64_t FluxTaps::latest() const
{
    std::int64_t last = -1;
    for (const auto* tap : all())
        if (tap->armed())
            last = std::max(last, tap->latest());
    return last;
}

double FluxTaps::sampleRate() const
{
    const auto taps = all();
    return taps.empty() ? 48000.0 : taps.front()->sampleRate();
}

void FluxTaps::read(const Place& place, std::int64_t from, int count, float* out) const
{
    std::fill(out, out + std::max(0, count), 0.0f);
    std::vector<float> part(static_cast<std::size_t>(std::max(0, count)));
    for (const auto* tap : tapsAt(place))
    {
        tap->read(from, count, part.data());
        for (std::size_t index = 0; index < part.size(); ++index)
            out[index] += part[index];
    }
}

} // namespace daw::engine
