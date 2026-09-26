#include "daw/domain/project/Automation.h"

#include "daw/domain/project/ProjectState.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace daw::domain
{
namespace
{

template <typename IdType>
Result<IdType> idAt(const Value& value, std::string_view key)
{
    auto text = value.stringAt(key);
    if (!text)
        return text.error();

    auto parsed = IdType::parse(text.value());
    if (!parsed)
        return fail(parsed.error().code, std::string{key} + ": " + parsed.error().message);

    return parsed.value();
}

// Tracktion's own segment, re-derived from tracktion_Bezier.h and
// AutomationCurve::getValueAt so the domain gives the value the engine plays.
// x is the position, y the value, both in the space the engine interpolates.
struct Segment
{
    double x1, y1, x2, y2, curve;
};

std::pair<double, double> bezierPoint(double x1, double y1, double x2, double y2, double c)
{
    const auto run = x2 - x1;
    const auto xc = x1 + run / 2;
    const auto x = xc - run / 2 * -c;

    if (y2 > y1)
    {
        const auto rise = y2 - y1;
        return {x, y1 + rise / 2 + rise / 2 * -c};
    }

    const auto rise = y1 - y2;
    return {x, y2 + rise / 2 - rise / 2 * -c};
}

double bezierYFromX(double x, double x1, double y1, double xb, double yb, double x2, double y2)
{
    if (x1 == x2 || y1 == y2)
        return y1;
    if (x <= x1)
        return y1;
    if (x >= x2)
        return y2;

    const auto a = x1 - 2 * xb + x2;
    const auto b = -2 * x1 + 2 * xb;
    const auto c = x1 - x;

    double t = 0.0;
    if (a == 0.0)
    {
        t = -c / b;
    }
    else
    {
        t = (-b + std::sqrt(b * b - 4 * a * c)) / (2 * a);
        if (t < 0.0 || t > 1.0)
            t = (-b - std::sqrt(b * b - 4 * a * c)) / (2 * a);
    }

    return (1 - t) * (1 - t) * y1 + 2 * t * (1 - t) * yb + t * t * y2;
}

double segmentValue(const Segment& s, double x)
{
    if (s.curve == 0.0)
        return s.y1 + (x - s.x1) / (s.x2 - s.x1) * (s.y2 - s.y1);

    const auto doubled = std::clamp(s.curve * 2.0, -1.0, 1.0);
    const auto [bx, by] = bezierPoint(s.x1, s.y1, s.x2, s.y2, doubled);

    if (s.curve >= -0.5 && s.curve <= 0.5)
        return bezierYFromX(x, s.x1, s.y1, bx, by, s.x2, s.y2);

    // Past a half, the curve holds flat at one end and bends over the rest.
    const auto minic = (std::abs(s.curve) - 0.5) * 2.0;
    const auto run = minic * (s.x2 - s.x1);
    const auto rise = minic * (s.y2 > s.y1 ? s.y2 - s.y1 : s.y1 - s.y2);

    double ex1 = s.x1;
    double ey1 = s.y1;
    double ex2 = s.x2;
    double ey2 = s.y2;
    if (s.curve > 0)
    {
        ex1 = s.x1 + run;
        ey2 = s.y1 < s.y2 ? s.y2 - rise : s.y2 + rise;
    }
    else
    {
        ey1 = s.y1 < s.y2 ? s.y1 + rise : s.y1 - rise;
        ex2 = s.x2 - run;
    }

    if (x >= s.x1 && x <= ex1)
        return ey1;
    if (x >= ex2 && x <= s.x2)
        return ey2;

    return bezierYFromX(x, ex1, ey1, bx, by, ex2, ey2);
}

bool isFinite(double value)
{
    return std::isfinite(value);
}

} // namespace

// ---------------------------------------------------------------------------
// volume fader position
// ---------------------------------------------------------------------------

double volumeFaderPosition(double volumeDb) noexcept
{
    return volumeDb > ProjectState::minVolumeDb ? std::exp((volumeDb - 6.0) / 20.0) : 0.0;
}

double volumeFromFaderPosition(double position) noexcept
{
    if (!(position > 0.0))
        return ProjectState::minVolumeDb;
    return std::clamp(20.0 * std::log(position) + 6.0, ProjectState::minVolumeDb, ProjectState::maxVolumeDb);
}

// ---------------------------------------------------------------------------
// AutomationTarget
// ---------------------------------------------------------------------------

AutomationTarget AutomationTarget::volumeOf(TrackId strip)
{
    AutomationTarget target{};
    target.kind = Kind::volume;
    target.strip = strip;
    return target;
}

AutomationTarget AutomationTarget::panOf(TrackId strip)
{
    AutomationTarget target{};
    target.kind = Kind::pan;
    target.strip = strip;
    return target;
}

AutomationTarget AutomationTarget::parameterOf(PluginId plugin, std::string paramId)
{
    AutomationTarget target{};
    target.kind = Kind::pluginParameter;
    target.plugin = plugin;
    target.paramId = std::move(paramId);
    return target;
}

double AutomationTarget::lowest() const noexcept
{
    switch (kind)
    {
    case Kind::volume:
        return ProjectState::minVolumeDb;
    case Kind::pan:
        return ProjectState::minPan;
    case Kind::pluginParameter:
        break;
    }
    return 0.0;
}

double AutomationTarget::highest() const noexcept
{
    switch (kind)
    {
    case Kind::volume:
        return ProjectState::maxVolumeDb;
    case Kind::pan:
        return ProjectState::maxPan;
    case Kind::pluginParameter:
        break;
    }
    return 1.0;
}

std::string_view AutomationTarget::kindName(Kind kind) noexcept
{
    switch (kind)
    {
    case Kind::volume:
        return "volume";
    case Kind::pan:
        return "pan";
    case Kind::pluginParameter:
        break;
    }
    return "plugin_parameter";
}

Result<void> AutomationTarget::validate() const
{
    if (kind == Kind::pluginParameter)
    {
        if (plugin.isNil())
            return fail(ErrorCode::invalidArgument, "a plugin parameter line names its plugin");
        if (paramId.empty())
            return fail(ErrorCode::invalidArgument, "a plugin parameter line names its parameter");
        if (!strip.isNil())
            return fail(ErrorCode::invalidArgument, "a plugin parameter line names no strip");
        return {};
    }

    if (strip.isNil())
        return fail(ErrorCode::invalidArgument, "a volume or pan line names its strip");
    if (!plugin.isNil() || !paramId.empty())
        return fail(ErrorCode::invalidArgument, "a volume or pan line names no plugin");
    return {};
}

Value AutomationTarget::toValue() const
{
    Value::Object members{{"kind", Value{std::string{kindName(kind)}}}};
    if (kind == Kind::pluginParameter)
    {
        members.emplace_back("plugin", Value{plugin.toString()});
        members.emplace_back("paramId", Value{paramId});
    }
    else
    {
        members.emplace_back("strip", Value{strip.toString()});
    }
    return Value::object(std::move(members));
}

Result<AutomationTarget> AutomationTarget::fromValue(const Value& value)
{
    auto kind = value.stringAt("kind");
    if (!kind)
        return kind.error();

    AutomationTarget target{};
    if (kind.value() == kindName(Kind::volume) || kind.value() == kindName(Kind::pan))
    {
        target.kind = kind.value() == kindName(Kind::volume) ? Kind::volume : Kind::pan;
        auto strip = idAt<TrackId>(value, "strip");
        if (!strip)
            return strip.error();
        target.strip = strip.value();
    }
    else if (kind.value() == kindName(Kind::pluginParameter))
    {
        target.kind = Kind::pluginParameter;
        auto plugin = idAt<PluginId>(value, "plugin");
        if (!plugin)
            return plugin.error();
        auto paramId = value.stringAt("paramId");
        if (!paramId)
            return paramId.error();
        target.plugin = plugin.value();
        target.paramId = paramId.value();
    }
    else
    {
        return fail(ErrorCode::invalidPayload, "unknown automation target: " + kind.value());
    }

    if (auto valid = target.validate(); !valid)
        return valid.error();
    return target;
}

bool operator==(const AutomationTarget& lhs, const AutomationTarget& rhs)
{
    return lhs.kind == rhs.kind && lhs.strip == rhs.strip && lhs.plugin == rhs.plugin &&
           lhs.paramId == rhs.paramId;
}

// ---------------------------------------------------------------------------
// AutomationPoint
// ---------------------------------------------------------------------------

Result<void> AutomationPoint::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "automation point identifier is nil");
    if (!isFinite(beats) || beats < 0.0)
        return fail(ErrorCode::invalidArgument, "an automation point sits at or after the timeline origin");
    if (!isFinite(value))
        return fail(ErrorCode::invalidArgument, "an automation value must be finite");
    if (!(curve >= lowestCurve && curve <= highestCurve))
        return fail(ErrorCode::invalidArgument, "an automation curve lies between -1 and +1");
    return {};
}

Value AutomationPoint::toValue() const
{
    return Value::object({{"id", Value{id.toString()}},
                          {"beats", Value{beats}},
                          {"value", Value{value}},
                          {"curve", Value{curve}}});
}

Result<AutomationPoint> AutomationPoint::fromValue(const Value& value)
{
    auto id = idAt<AutomationPointId>(value, "id");
    if (!id)
        return id.error();
    auto beats = value.doubleAt("beats");
    if (!beats)
        return beats.error();
    auto level = value.doubleAt("value");
    if (!level)
        return level.error();

    AutomationPoint point{};
    point.id = id.value();
    point.beats = beats.value();
    point.value = level.value();

    // Absent means straight: the one shape nobody has to ask for.
    if (value.contains("curve"))
    {
        auto curve = value.doubleAt("curve");
        if (!curve)
            return curve.error();
        point.curve = curve.value();
    }

    if (auto valid = point.validate(); !valid)
        return valid.error();
    return point;
}

bool operator==(const AutomationPoint& lhs, const AutomationPoint& rhs)
{
    return lhs.id == rhs.id && lhs.beats == rhs.beats && lhs.value == rhs.value && lhs.curve == rhs.curve;
}

// ---------------------------------------------------------------------------
// AutomationLine
// ---------------------------------------------------------------------------

const AutomationPoint* AutomationLine::findPoint(AutomationPointId pointId) const noexcept
{
    const auto found = std::find_if(points.begin(),
                                    points.end(),
                                    [pointId](const AutomationPoint& point) { return point.id == pointId; });
    return found != points.end() ? &*found : nullptr;
}

double AutomationLine::valueAt(double beats) const noexcept
{
    if (points.empty())
        return 0.0;
    if (beats <= points.front().beats)
        return points.front().value;
    if (beats >= points.back().beats)
        return points.back().value;

    const auto next =
        std::upper_bound(points.begin(),
                         points.end(),
                         beats,
                         [](double at, const AutomationPoint& point) { return at < point.beats; });
    const auto& p1 = *(next - 1);
    const auto& p2 = *next;

    // The volume bends in the space the engine interpolates it in, the fader
    // position; every other target is interpolated as it is stored.
    const auto volume = target.kind == AutomationTarget::Kind::volume;
    const auto y1 = volume ? volumeFaderPosition(p1.value) : p1.value;
    const auto y2 = volume ? volumeFaderPosition(p2.value) : p2.value;

    const auto y = segmentValue(Segment{p1.beats, y1, p2.beats, y2, p1.curve}, beats);
    return volume ? volumeFromFaderPosition(y) : y;
}

Result<void> AutomationLine::validate() const
{
    if (id.isNil())
        return fail(ErrorCode::invalidArgument, "automation line identifier is nil");
    if (auto valid = target.validate(); !valid)
        return valid;

    for (std::size_t index = 0; index < points.size(); ++index)
    {
        const auto& point = points[index];
        if (auto valid = point.validate(); !valid)
            return valid;
        if (point.value < target.lowest() || point.value > target.highest())
            return fail(ErrorCode::invalidArgument, "automation value out of range for this target");
        if (index > 0 && !(points[index - 1].beats < point.beats))
            return fail(ErrorCode::invalidArgument, "automation points are sorted, one per beat");
        for (std::size_t other = 0; other < index; ++other)
        {
            if (points[other].id == point.id)
                return fail(ErrorCode::conflict, "automation point already exists: " + point.id.toString());
        }
    }
    return {};
}

Value AutomationLine::toValue() const
{
    Value::Array serialised;
    serialised.reserve(points.size());
    for (const auto& point : points)
        serialised.push_back(point.toValue());

    return Value::object({{"id", Value{id.toString()}},
                          {"target", target.toValue()},
                          {"points", Value::array(std::move(serialised))}});
}

Result<AutomationLine> AutomationLine::fromValue(const Value& value)
{
    auto id = idAt<AutomationLineId>(value, "id");
    if (!id)
        return id.error();

    const auto* targetValue = value.find("target");
    if (targetValue == nullptr)
        return fail(ErrorCode::invalidPayload, "missing key: target");
    auto target = AutomationTarget::fromValue(*targetValue);
    if (!target)
        return target.error();

    AutomationLine line{};
    line.id = id.value();
    line.target = std::move(target).value();

    if (const auto* pointsValue = value.find("points"); pointsValue != nullptr)
    {
        const auto* items = pointsValue->asArray();
        if (items == nullptr)
            return fail(ErrorCode::invalidPayload, "points must be an array");
        for (const auto& item : *items)
        {
            auto point = AutomationPoint::fromValue(item);
            if (!point)
                return point.error();
            line.points.push_back(point.value());
        }
    }

    if (auto valid = line.validate(); !valid)
        return valid.error();
    return line;
}

bool operator==(const AutomationLine& lhs, const AutomationLine& rhs)
{
    return lhs.id == rhs.id && lhs.target == rhs.target && lhs.points == rhs.points;
}

// ---------------------------------------------------------------------------
// ProjectState: automation
// ---------------------------------------------------------------------------

const AutomationLine* ProjectState::findAutomationLine(AutomationLineId id) const noexcept
{
    const auto found = std::find_if(
        automation_.begin(), automation_.end(), [id](const AutomationLine& line) { return line.id == id; });
    return found != automation_.end() ? &*found : nullptr;
}

AutomationLine* ProjectState::findAutomationLineMutable(AutomationLineId id) noexcept
{
    const auto found = std::find_if(
        automation_.begin(), automation_.end(), [id](const AutomationLine& line) { return line.id == id; });
    return found != automation_.end() ? &*found : nullptr;
}

const AutomationLine* ProjectState::findAutomationLineFor(const AutomationTarget& target) const noexcept
{
    const auto found = std::find_if(automation_.begin(),
                                    automation_.end(),
                                    [&target](const AutomationLine& line) { return line.target == target; });
    return found != automation_.end() ? &*found : nullptr;
}

Result<std::size_t> ProjectState::automationLineIndex(AutomationLineId id) const
{
    for (std::size_t index = 0; index < automation_.size(); ++index)
    {
        if (automation_[index].id == id)
            return index;
    }
    return fail(ErrorCode::notFound, "no automation line " + id.toString());
}

Result<void> ProjectState::checkAutomationTarget(const AutomationTarget& target) const
{
    if (auto valid = target.validate(); !valid)
        return valid;

    if (target.kind == AutomationTarget::Kind::pluginParameter)
    {
        if (findPlugin(target.plugin) == nullptr)
            return fail(ErrorCode::notFound, "no such plugin: " + target.plugin.toString());
        return {};
    }

    if (findStrip(target.strip) == nullptr)
        return fail(ErrorCode::notFound, "no such strip: " + target.strip.toString());
    return {};
}

std::vector<AutomationLineId> ProjectState::automationOfStrip(TrackId strip) const
{
    const auto* track = findStrip(strip);
    std::vector<AutomationLineId> lines;
    for (const auto& line : automation_)
    {
        if (line.target.kind != AutomationTarget::Kind::pluginParameter)
        {
            if (line.target.strip == strip)
                lines.push_back(line.id);
            continue;
        }

        if (track == nullptr)
            continue;
        const auto onStrip =
            std::any_of(track->plugins.begin(),
                        track->plugins.end(),
                        [&line](const PluginInstance& plugin) { return plugin.id == line.target.plugin; });
        if (onStrip)
            lines.push_back(line.id);
    }
    return lines;
}

std::vector<AutomationLineId> ProjectState::automationOfPlugin(PluginId plugin) const
{
    std::vector<AutomationLineId> lines;
    for (const auto& line : automation_)
    {
        if (line.target.kind == AutomationTarget::Kind::pluginParameter && line.target.plugin == plugin)
            lines.push_back(line.id);
    }
    return lines;
}

void ProjectState::dropAutomation(const std::vector<AutomationLineId>& lines)
{
    automation_.erase(std::remove_if(automation_.begin(),
                                     automation_.end(),
                                     [&lines](const AutomationLine& line) {
                                         return std::find(lines.begin(), lines.end(), line.id) != lines.end();
                                     }),
                      automation_.end());
}

Result<void> ProjectState::insertAutomationLine(AutomationLine line, std::size_t index)
{
    if (auto valid = line.validate(); !valid)
        return valid;
    if (auto target = checkAutomationTarget(line.target); !target)
        return target;
    if (findAutomationLine(line.id) != nullptr)
        return fail(ErrorCode::conflict, "automation line already exists: " + line.id.toString());

    // One line per target: two would be two answers to "what is the volume
    // of the kick at bar 12".
    if (findAutomationLineFor(line.target) != nullptr)
        return fail(ErrorCode::conflict, "this target already has an automation line");

    const auto at = std::min(index, automation_.size());
    automation_.insert(automation_.begin() + static_cast<std::ptrdiff_t>(at), std::move(line));
    return {};
}

Result<void> ProjectState::removeAutomationLine(AutomationLineId id)
{
    auto index = automationLineIndex(id);
    if (!index)
        return index.error();
    automation_.erase(automation_.begin() + static_cast<std::ptrdiff_t>(index.value()));
    return {};
}

Result<void> ProjectState::insertAutomationPoint(AutomationLineId lineId, AutomationPoint point)
{
    auto* line = findAutomationLineMutable(lineId);
    if (line == nullptr)
        return fail(ErrorCode::notFound, "no automation line " + lineId.toString());

    auto changed = *line;
    const auto at = std::upper_bound(changed.points.begin(),
                                     changed.points.end(),
                                     point.beats,
                                     [](double beats, const AutomationPoint& existing)
                                     { return beats < existing.beats; });
    changed.points.insert(at, point);
    if (auto valid = changed.validate(); !valid)
        return valid;

    *line = std::move(changed);
    return {};
}

Result<void> ProjectState::removeAutomationPoint(AutomationLineId lineId, AutomationPointId pointId)
{
    auto* line = findAutomationLineMutable(lineId);
    if (line == nullptr)
        return fail(ErrorCode::notFound, "no automation line " + lineId.toString());

    const auto found = std::find_if(line->points.begin(),
                                    line->points.end(),
                                    [pointId](const AutomationPoint& point) { return point.id == pointId; });
    if (found == line->points.end())
        return fail(ErrorCode::notFound, "no automation point " + pointId.toString());

    line->points.erase(found);
    return {};
}

Result<void> ProjectState::moveAutomationPoint(AutomationLineId lineId,
                                               AutomationPointId pointId,
                                               double beats,
                                               double value)
{
    const auto* line = findAutomationLine(lineId);
    if (line == nullptr)
        return fail(ErrorCode::notFound, "no automation line " + lineId.toString());
    const auto* existing = line->findPoint(pointId);
    if (existing == nullptr)
        return fail(ErrorCode::notFound, "no automation point " + pointId.toString());

    // Moving is taking out and putting back at the new place, so the order
    // stays sorted and a point cannot be dragged onto another one.
    auto moved = *existing;
    moved.beats = beats;
    moved.value = value;

    auto changed = *line;
    changed.points.erase(std::find_if(changed.points.begin(),
                                      changed.points.end(),
                                      [pointId](const AutomationPoint& point)
                                      { return point.id == pointId; }));
    const auto at =
        std::upper_bound(changed.points.begin(),
                         changed.points.end(),
                         beats,
                         [](double at, const AutomationPoint& point) { return at < point.beats; });
    changed.points.insert(at, moved);
    if (auto valid = changed.validate(); !valid)
        return valid;

    *findAutomationLineMutable(lineId) = std::move(changed);
    return {};
}

Result<void>
ProjectState::setAutomationCurve(AutomationLineId lineId, AutomationPointId pointId, double curve)
{
    auto* line = findAutomationLineMutable(lineId);
    if (line == nullptr)
        return fail(ErrorCode::notFound, "no automation line " + lineId.toString());

    for (auto& point : line->points)
    {
        if (point.id != pointId)
            continue;
        if (!(curve >= AutomationPoint::lowestCurve && curve <= AutomationPoint::highestCurve))
            return fail(ErrorCode::invalidArgument, "an automation curve lies between -1 and +1");
        point.curve = curve;
        return {};
    }
    return fail(ErrorCode::notFound, "no automation point " + pointId.toString());
}

} // namespace daw::domain
