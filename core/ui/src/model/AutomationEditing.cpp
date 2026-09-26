#include "daw/ui/model/AutomationEditing.h"

#include "daw/domain/commands/AutomationCommands.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace daw::ui::automationEditing
{
namespace
{

using Kind = domain::AutomationTarget::Kind;

// The strips in screen order: channels, buses, the master.
std::vector<const domain::Track*> strips(const domain::ProjectState& state)
{
    std::vector<const domain::Track*> all;
    for (const auto& track : state.tracks())
        all.push_back(&track);
    for (const auto& bus : state.buses())
        all.push_back(&bus);
    all.push_back(&state.master());
    return all;
}

// The rank of a line inside its strip: volume, pan, then its plugin's rank in
// the chain.
std::size_t rankInStrip(const domain::ProjectState& state, const domain::AutomationLine& line)
{
    switch (line.target.kind)
    {
    case Kind::volume:
        return 0;
    case Kind::pan:
        return 1;
    case Kind::pluginParameter:
        break;
    }
    const auto location = state.pluginLocation(line.target.plugin);
    return 2 + (location ? location.value().index : 0);
}

} // namespace

domain::TrackId stripOf(const domain::ProjectState& state, const domain::AutomationLine& line)
{
    if (line.target.kind != Kind::pluginParameter)
        return line.target.strip;
    const auto location = state.pluginLocation(line.target.plugin);
    return location ? location.value().trackId : domain::TrackId{};
}

std::vector<const domain::AutomationLine*> ordered(const domain::ProjectState& state)
{
    std::vector<const domain::AutomationLine*> lines;
    for (const auto* strip : strips(state))
    {
        std::vector<const domain::AutomationLine*> own;
        for (const auto& line : state.automation())
        {
            if (stripOf(state, line) == strip->id)
                own.push_back(&line);
        }
        // Stable: two parameters of one plugin keep the order they were
        // automated in.
        std::stable_sort(own.begin(),
                         own.end(),
                         [&state](const domain::AutomationLine* lhs, const domain::AutomationLine* rhs)
                         { return rankInStrip(state, *lhs) < rankInStrip(state, *rhs); });
        lines.insert(lines.end(), own.begin(), own.end());
    }
    return lines;
}

std::string label(const domain::ProjectState& state, const domain::AutomationLine& line)
{
    const auto* strip = state.findStrip(stripOf(state, line));
    auto name =
        strip == nullptr || strip->name.empty()
            ? std::string{strip != nullptr && strip->id == domain::ProjectState::masterTrackId() ? "Master"
                                                                                                 : "?"}
            : strip->name;

    switch (line.target.kind)
    {
    case Kind::volume:
        return name + " · Volume";
    case Kind::pan:
        return name + " · Pan";
    case Kind::pluginParameter:
        break;
    }

    const auto* plugin = state.findPlugin(line.target.plugin);
    const auto pluginName = plugin != nullptr ? plugin->ref.name : std::string{"?"};
    return name + " · " + pluginName + " · " + line.target.paramId;
}

double heightOf(const domain::AutomationTarget& target, double value) noexcept
{
    switch (target.kind)
    {
    case Kind::volume:
        return domain::volumeFaderPosition(value);
    case Kind::pan:
        return (value - domain::ProjectState::minPan) /
               (domain::ProjectState::maxPan - domain::ProjectState::minPan);
    case Kind::pluginParameter:
        break;
    }
    return value;
}

double valueAtHeight(const domain::AutomationTarget& target, double height) noexcept
{
    const auto clamped = std::clamp(height, 0.0, 1.0);
    switch (target.kind)
    {
    case Kind::volume:
        return domain::volumeFromFaderPosition(clamped);
    case Kind::pan:
        return domain::ProjectState::minPan +
               clamped * (domain::ProjectState::maxPan - domain::ProjectState::minPan);
    case Kind::pluginParameter:
        break;
    }
    return clamped;
}

double stepValue(const domain::AutomationTarget& target, double value, int notches) noexcept
{
    double step = 0.01;
    if (target.kind == Kind::volume)
        step = 1.0;
    else if (target.kind == Kind::pan)
        step = 0.05;

    // On the grid of the step, so a notch lands on a round figure.
    const auto moved = std::round(value / step + notches) * step;
    return std::clamp(moved, target.lowest(), target.highest());
}

double stepCurve(double curve, int notches) noexcept
{
    const auto moved = std::round(curve * 10.0 + notches) / 10.0;
    return std::clamp(moved, domain::AutomationPoint::lowestCurve, domain::AutomationPoint::highestCurve);
}

double staticValue(const domain::ProjectState& state, const domain::AutomationTarget& target)
{
    if (target.kind == Kind::pluginParameter)
    {
        const auto* plugin = state.findPlugin(target.plugin);
        const auto* param = plugin != nullptr ? plugin->findParam(target.paramId) : nullptr;
        return param != nullptr ? param->value : 0.5;
    }

    const auto* strip = state.findStrip(target.strip);
    if (strip == nullptr)
        return 0.0;
    return target.kind == Kind::volume ? strip->volumeDb : strip->pan;
}

domain::AutomationLineId
open(domain::CommandBus& bus, const domain::ProjectState& state, const domain::AutomationTarget& target)
{
    if (const auto* existing = state.findAutomationLineFor(target); existing != nullptr)
        return existing->id;

    const auto lineId = domain::AutomationLineId::generate();
    if (!bus.execute(std::make_unique<domain::CreateAutomationLine>(lineId, target)).ok())
        return {};
    return lineId;
}

} // namespace daw::ui::automationEditing
