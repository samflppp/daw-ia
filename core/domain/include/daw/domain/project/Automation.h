#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/Value.h"

#include <string>
#include <string_view>
#include <vector>

namespace daw::domain
{

// --- automation --------------------------------------------------------------
//
// The model validated at S13, in five decisions:
//
//   target     a line drives one thing: the volume or the pan of a strip, or
//              one parameter of one plugin, named by the format's own id and
//              never by an index (the S4 rule). One line per target at most.
//   values     in the units of the domain: dB within the fader bounds, pan
//              from -1 to +1, a plugin parameter normalised 0..1. How they
//              become Tracktion's is the projection's business, like the pan
//              law.
//   curve      Tracktion's, not an invention: a number from -1 to +1 carried
//              by a point and shaping the segment that leaves it, 0 straight.
//              Before the first point the line holds its value, after the
//              last one too; a line with no point drives nothing.
//   position   in beats like every other content, so a tempo change moves
//              nothing in the domain.
//   ownership  the arrangement: a line is absolute on the timeline, it does
//              not repeat with a pattern, and pattern mode does not play it.
//              A line owned by a pattern would be an additional field.

struct AutomationTarget
{
    enum class Kind
    {
        volume,
        pan,
        pluginParameter
    };

    Kind kind{Kind::volume};

    // The strip whose fader or pan is driven: a channel, a bus or the master.
    // Nil for a plugin parameter: the plugin already says where it is, and a
    // strip repeated here would be a second truth waiting to disagree.
    TrackId strip{};

    // Set for a plugin parameter only.
    PluginId plugin{};
    std::string paramId;

    [[nodiscard]] static AutomationTarget volumeOf(TrackId strip);
    [[nodiscard]] static AutomationTarget panOf(TrackId strip);
    [[nodiscard]] static AutomationTarget parameterOf(PluginId plugin, std::string paramId);

    // The range a value of this target must sit in.
    [[nodiscard]] double lowest() const noexcept;
    [[nodiscard]] double highest() const noexcept;

    // Shape only: that the right fields are set for the kind. Whether the
    // strip or the plugin exists is the project's question.
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<AutomationTarget> fromValue(const Value& value);

    [[nodiscard]] static std::string_view kindName(Kind kind) noexcept;

    friend bool operator==(const AutomationTarget& lhs, const AutomationTarget& rhs);
};

struct AutomationPoint
{
    static constexpr double lowestCurve = -1.0;
    static constexpr double highestCurve = 1.0;

    AutomationPointId id{};
    double beats{0.0};
    double value{0.0};
    double curve{0.0};

    // The value is checked against its target by the line, which knows it.
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<AutomationPoint> fromValue(const Value& value);

    friend bool operator==(const AutomationPoint& lhs, const AutomationPoint& rhs);
};

struct AutomationLine
{
    AutomationLineId id{};
    AutomationTarget target{};

    // Sorted by beats, never two on the same beat: the value at a beat has to
    // be one answer, not the order two points happen to be stored in.
    std::vector<AutomationPoint> points;

    [[nodiscard]] const AutomationPoint* findPoint(AutomationPointId id) const noexcept;

    // The value the line gives at that beat, the curve included, in the
    // target's units. Undefined for a line with no point: the caller asks
    // points.empty() first, because an empty line drives nothing.
    [[nodiscard]] double valueAt(double beats) const noexcept;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] Value toValue() const;
    [[nodiscard]] static Result<AutomationLine> fromValue(const Value& value);

    friend bool operator==(const AutomationLine& lhs, const AutomationLine& rhs);
};

// Tracktion's volume fader position, the space in which it interpolates a
// volume curve: exp((dB - 6) / 20), and 0 at the floor. Written here because
// the value a line gives between two points has to be the value the engine
// plays, and for the volume that is not a straight line in dB.
[[nodiscard]] double volumeFaderPosition(double volumeDb) noexcept;
[[nodiscard]] double volumeFromFaderPosition(double position) noexcept;

} // namespace daw::domain
