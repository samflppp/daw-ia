#include "LevelMonitor.h"

#include "daw/engine/MeterTap.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace daw::app
{
namespace
{

// A tenth of a decibel: finer than any ear, and a copilot does not need six
// digits to compare two tracks.
domain::Value decibels(float value)
{
    return domain::Value{std::round(static_cast<double>(value) * 10.0) / 10.0};
}

domain::Value stripValue(const engine::StripLevel& level, const std::string& name)
{
    return domain::Value::object({{"strip", domain::Value{level.strip}},
                                  {"name", domain::Value{name}},
                                  {"peakDb", decibels(level.peakDb)},
                                  {"rmsDb", decibels(level.rmsDb)},
                                  {"peakLeftDb", decibels(level.peakLeftDb)},
                                  {"peakRightDb", decibels(level.peakRightDb)},
                                  {"rmsLeftDb", decibels(level.rmsLeftDb)},
                                  {"rmsRightDb", decibels(level.rmsRightDb)},
                                  {"over", domain::Value{level.over}}});
}

} // namespace

LevelMonitor::LevelMonitor(tracktion::Edit& edit)
    : edit_{edit}
    , meters_{edit}
{
    startTimerHz(pollHz);
}

LevelMonitor::~LevelMonitor()
{
    stopTimer();
}

void LevelMonitor::timerCallback()
{
    meters_.poll();
    sendChangeMessage();
}

domain::Value LevelMonitor::toValue(const domain::ProjectState& state) const
{
    const auto measured = meters_.levels();
    const auto find = [&measured](const std::string& strip) -> engine::StripLevel
    {
        const auto found =
            std::find_if(measured.begin(),
                         measured.end(),
                         [&strip](const engine::StripLevel& level) { return level.strip == strip; });
        if (found != measured.end())
            return *found;

        engine::StripLevel silent{};
        silent.strip = strip;
        return silent;
    };

    domain::Value::Array strips;
    strips.reserve(state.tracks().size() + 1);
    for (const auto& track : state.tracks())
        strips.push_back(stripValue(find(track.id.toString()), track.name));

    strips.push_back(stripValue(find(engine::MeterTapPlugin::masterStrip.toStdString()), "Master"));

    return domain::Value::object({{"playing", domain::Value{edit_.getTransport().isPlaying()}},
                                  {"windowSeconds", domain::Value{engine::LevelMeters::windowSeconds}},
                                  {"strips", domain::Value::array(std::move(strips))}});
}

} // namespace daw::app
