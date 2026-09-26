#include "daw/ui/model/TempoEditing.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <string>

namespace daw::ui::tempoEditing
{
namespace
{

[[nodiscard]] std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

[[nodiscard]] std::optional<int> parseInteger(std::string_view text)
{
    text = trimmed(text);
    int value = 0;
    const auto* end = text.data() + text.size();
    const auto [last, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || last != end)
        return std::nullopt;
    return value;
}

} // namespace

double projectTempo(const domain::ProjectState& state) noexcept
{
    return state.tempoPoints().front().beatsPerMinute;
}

double stepTempo(double bpm, int notches) noexcept
{
    if (notches == 0)
        return bpm;

    // The first notch lands on the whole number, the next ones step by one.
    const auto landed = notches > 0 ? std::floor(bpm) + 1.0 : std::ceil(bpm) - 1.0;
    const auto stepped = landed + static_cast<double>(notches > 0 ? notches - 1 : notches + 1);
    return std::clamp(stepped, domain::ProjectState::minTempo, domain::ProjectState::maxTempo);
}

std::optional<double> parseTempo(std::string_view text)
{
    std::string normalised{trimmed(text)};
    std::replace(normalised.begin(), normalised.end(), ',', '.');

    // A stream in the classic locale rather than from_chars: every standard
    // library the CI builds with reads a double this way, and the decimal
    // point is a point whatever the machine's locale says.
    std::istringstream stream{normalised};
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> std::noskipws >> value;
    if (normalised.empty() || stream.fail() || !stream.eof())
        return std::nullopt;
    if (value < domain::ProjectState::minTempo || value > domain::ProjectState::maxTempo)
        return std::nullopt;
    return value;
}

domain::TimeSignature stepSignature(domain::TimeSignature signature, int notches) noexcept
{
    signature.numerator = std::clamp(signature.numerator + notches,
                                     domain::TimeSignature::lowestNumerator,
                                     domain::TimeSignature::highestNumerator);
    return signature;
}

std::optional<domain::TimeSignature> parseSignature(std::string_view text)
{
    const auto slash = text.find('/');
    if (slash == std::string_view::npos)
        return std::nullopt;

    const auto numerator = parseInteger(text.substr(0, slash));
    const auto denominator = parseInteger(text.substr(slash + 1));
    if (!numerator.has_value() || !denominator.has_value())
        return std::nullopt;

    const domain::TimeSignature signature{*numerator, *denominator};
    if (!signature.validate())
        return std::nullopt;
    return signature;
}

double automationStart(const domain::ProjectState& state, double playheadBeats) noexcept
{
    const auto bar = state.beatsPerBar();
    auto start = std::max(bar, std::floor(std::max(0.0, playheadBeats) / bar + 1e-9) * bar);

    // A bar that already holds a change is not a new one: the next free bar.
    const auto taken = [&state](double beats)
    {
        const auto& points = state.tempoPoints();
        return std::any_of(points.begin(),
                           points.end(),
                           [beats](const domain::TempoPoint& point)
                           { return std::abs(point.startBeats - beats) < 1e-9; });
    };
    while (taken(start))
        start += bar;
    return start;
}

bool isAutomated(const domain::ProjectState& state) noexcept
{
    return state.tempoPoints().size() > 1;
}

} // namespace daw::ui::tempoEditing
