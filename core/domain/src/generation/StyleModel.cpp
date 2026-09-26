#include "daw/domain/generation/StyleModel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace daw::domain::generation
{
namespace
{

constexpr int stepsPerBar = 16;
constexpr double alpha = 2.0;

constexpr std::array<std::string_view, 4> roleNames{"melody", "bass", "chords", "rhythm"};
constexpr std::array<std::string_view, 5> tableNames{
    "rhythm", "duration", "interval", "degree", "progression"};

[[nodiscard]] std::size_t indexOf(Role role) noexcept
{
    return static_cast<std::size_t>(role);
}

[[nodiscard]] Table& tableOf(RoleStyle& style, std::size_t index) noexcept
{
    switch (index)
    {
    case 0:
        return style.rhythm;
    case 1:
        return style.duration;
    case 2:
        return style.interval;
    case 3:
        return style.degree;
    default:
        return style.progression;
    }
}

[[nodiscard]] const Table& tableOf(const RoleStyle& style, std::size_t index) noexcept
{
    return tableOf(const_cast<RoleStyle&>(style), index);
}

[[nodiscard]] std::string joined(const std::vector<int>& values, std::size_t from)
{
    std::string out;
    for (auto i = from; i < values.size(); ++i)
    {
        if (i > from)
            out += ',';
        out += std::to_string(values[i]);
    }
    return out;
}

// All the suffixes of a history, longest first, the last three at most.
[[nodiscard]] std::vector<std::string> suffixes(std::string_view prefix, const std::vector<int>& history)
{
    std::vector<std::string> out;
    const auto first = history.size() > 3 ? history.size() - 3 : 0;
    for (auto from = first; from <= history.size(); ++from)
        out.push_back(std::string{prefix} + joined(history, from));
    return out;
}

// --- the hand-written style ----------------------------------------------------

void put(Table& table, const std::string& context, std::initializer_list<std::pair<int, double>> counts)
{
    auto& target = table[context];
    for (const auto& [x, count] : counts)
        target[x] += count;
}

// Onsets that land on a beat, then on an eighth, are preferred: that alone is
// most of what makes a line sit on a beat.
void rhythmOnTheBeat(Table& table, std::initializer_list<std::pair<int, double>> base)
{
    for (int position = 0; position < stepsPerBar; ++position)
    {
        auto& target = table["p" + std::to_string(position) + "|"];
        for (const auto& [interval, weight] : base)
        {
            const auto landing = (position + interval) % stepsPerBar;
            const auto factor = landing % 4 == 0 ? 2.0 : (landing % 2 == 0 ? 1.0 : 0.5);
            target[interval] += weight * factor * 3.0;
        }
    }
    put(table, "|", base);
}

void durationsFill(Table& table, double legato)
{
    for (int interval = 1; interval <= 16; ++interval)
    {
        auto& target = table[std::to_string(interval)];
        target[interval] += 3.0 * legato;
        if (interval > 1)
            target[interval - 1] += 1.0;
        if (interval >= 4)
            target[interval / 2] += 1.0;
    }
}

void velocities(RoleStyle& style, bool flat)
{
    for (int position = 0; position < stepsPerBar; ++position)
    {
        if (flat)
            style.velocity[position] = {92.0, 4.0};
        else if (position == 0)
            style.velocity[position] = {110.0, 6.0};
        else if (position % 4 == 0)
            style.velocity[position] = {100.0, 6.0};
        else if (position % 2 == 0)
            style.velocity[position] = {88.0, 8.0};
        else
            style.velocity[position] = {78.0, 8.0};
    }
}

// After a leap, a step back: the oldest rule of melody writing.
void gapFill(Table& table)
{
    for (const auto leap : {3, 4, 5, 7})
    {
        put(table, std::to_string(leap), {{-1, 6.0}, {-2, 3.0}, {0, 1.0}, {1, 0.5}});
        put(table, std::to_string(-leap), {{1, 6.0}, {2, 3.0}, {0, 1.0}, {-1, 0.5}});
    }
}

[[nodiscard]] Result<Table> tableFromValue(const Value& value, std::string_view name)
{
    Table out;
    if (value.isNull())
        return out;

    const auto* contexts = value.asObject();
    if (contexts == nullptr)
        return fail(ErrorCode::invalidPayload, std::string{name} + " must be an object");

    for (const auto& [context, counts] : *contexts)
    {
        const auto* entries = counts.asObject();
        if (entries == nullptr)
            return fail(ErrorCode::invalidPayload, std::string{name} + "[" + context + "] must be an object");

        auto& target = out[context];
        for (const auto& [x, count] : *entries)
        {
            char* end = nullptr;
            const auto parsed = std::strtol(x.c_str(), &end, 10);
            auto number = count.asDouble();
            if (end == x.c_str() || *end != '\0' || !number || number.value() < 0.0)
                return fail(ErrorCode::invalidPayload,
                            std::string{name} + "[" + context + "]: bad entry \"" + x + "\"");
            target[static_cast<int>(parsed)] = number.value();
        }
    }
    return out;
}

[[nodiscard]] Value tableToValue(const Table& table)
{
    Value::Object contexts;
    for (const auto& [context, counts] : table)
    {
        Value::Object entries;
        for (const auto& [x, count] : counts)
            entries.emplace_back(std::to_string(x), Value{count});
        contexts.emplace_back(context, Value::object(std::move(entries)));
    }
    return Value::object(std::move(contexts));
}

} // namespace

StyleModel StyleModel::fallback()
{
    StyleModel model;
    model.origin_ = "repli";

    auto& melody = model.roles_[indexOf(Role::melody)];
    rhythmOnTheBeat(melody.rhythm, {{1, 0.5}, {2, 4.0}, {3, 1.5}, {4, 4.0}, {6, 1.5}, {8, 1.5}});
    durationsFill(melody.duration, 1.0);
    put(melody.interval,
        "",
        {{0, 3.0},
         {1, 6.0},
         {-1, 6.0},
         {2, 4.0},
         {-2, 4.0},
         {3, 2.0},
         {-3, 2.0},
         {4, 1.5},
         {-4, 1.5},
         {5, 0.8},
         {-5, 0.8},
         {7, 1.0},
         {-7, 1.0}});
    gapFill(melody.interval);
    put(melody.degree, "s", {{0, 5.0}, {1, 1.0}, {2, 3.0}, {3, 1.0}, {4, 4.0}, {5, 1.0}, {6, 0.8}});
    put(melody.degree, "w", {{0, 2.0}, {1, 2.0}, {2, 2.0}, {3, 1.5}, {4, 2.0}, {5, 1.5}, {6, 1.0}});
    velocities(melody, false);

    auto& bass = model.roles_[indexOf(Role::bass)];
    rhythmOnTheBeat(bass.rhythm, {{2, 1.0}, {3, 3.0}, {4, 3.0}, {6, 2.0}, {8, 3.0}, {12, 1.0}, {16, 1.0}});
    durationsFill(bass.duration, 1.5);
    put(bass.interval,
        "",
        {{0, 5.0},
         {1, 2.0},
         {-1, 2.0},
         {2, 1.5},
         {-2, 1.5},
         {3, 2.0},
         {-3, 2.0},
         {4, 2.5},
         {-4, 2.5},
         {7, 2.0},
         {-7, 2.0}});
    put(bass.degree, "s", {{0, 8.0}, {1, 0.5}, {2, 1.0}, {3, 1.5}, {4, 3.0}, {5, 3.0}, {6, 1.5}});
    put(bass.degree, "w", {{0, 3.0}, {1, 1.0}, {2, 1.0}, {3, 1.0}, {4, 2.0}, {5, 1.5}, {6, 1.0}});
    velocities(bass, false);

    auto& chords = model.roles_[indexOf(Role::chords)];
    rhythmOnTheBeat(chords.rhythm, {{4, 1.0}, {8, 3.0}, {16, 6.0}});
    durationsFill(chords.duration, 2.0);
    put(chords.progression, "", {{0, 4.0}, {5, 3.0}, {2, 2.0}, {3, 2.0}, {6, 2.0}, {4, 1.5}});
    put(chords.progression, "0", {{5, 4.0}, {3, 2.0}, {6, 2.0}, {2, 1.0}, {4, 1.0}});
    put(chords.progression, "5", {{2, 2.0}, {6, 3.0}, {3, 2.0}, {0, 1.0}, {4, 1.0}});
    put(chords.progression, "2", {{6, 3.0}, {5, 1.0}, {0, 2.0}, {3, 1.0}});
    put(chords.progression, "6", {{0, 4.0}, {5, 1.0}, {2, 1.0}});
    put(chords.progression, "3", {{4, 2.0}, {0, 3.0}, {6, 1.0}});
    put(chords.progression, "4", {{0, 4.0}, {5, 2.0}});
    velocities(chords, true);

    auto& rhythm = model.roles_[indexOf(Role::rhythm)];
    rhythmOnTheBeat(rhythm.rhythm, {{1, 1.0}, {2, 5.0}, {3, 1.0}, {4, 4.0}, {8, 1.0}});
    durationsFill(rhythm.duration, 1.0);
    velocities(rhythm, false);

    return model;
}

const RoleStyle& StyleModel::role(Role role) const noexcept
{
    return roles_[indexOf(role)];
}

Result<StyleModel> StyleModel::fromValue(const Value& value)
{
    auto declared = value.stringAt("format");
    if (!declared || declared.value() != format)
        return fail(ErrorCode::invalidPayload, "not a style model: format must be \"daw-ia.style\"");

    auto declaredVersion = value.intAt("version");
    if (!declaredVersion || declaredVersion.value() != version)
        return fail(ErrorCode::invalidPayload, "style model: unsupported version");

    StyleModel model;
    auto origin = value.stringAt("origin");
    model.origin_ = origin ? origin.value() : std::string{"corpus"};

    const auto* roles = value.find("roles");
    if (roles == nullptr || !roles->isObject())
        return fail(ErrorCode::invalidPayload, "style model: roles must be an object");

    for (std::size_t r = 0; r < roleNames.size(); ++r)
    {
        const auto* role = roles->find(roleNames[r]);
        if (role == nullptr)
            continue;

        auto& style = model.roles_[r];
        for (std::size_t t = 0; t < tableNames.size(); ++t)
        {
            const auto* table = role->find(tableNames[t]);
            if (table == nullptr)
                continue;

            auto parsed =
                tableFromValue(*table, std::string{roleNames[r]} + "." + std::string{tableNames[t]});
            if (!parsed)
                return parsed.error();
            tableOf(style, t) = std::move(parsed).value();
        }

        if (const auto* velocity = role->find("velocity"); velocity != nullptr && velocity->isObject())
        {
            for (const auto& [position, pair] : *velocity->asObject())
            {
                const auto* items = pair.asArray();
                if (items == nullptr || items->size() != 2 || !(*items)[0].asDouble() ||
                    !(*items)[1].asDouble())
                    return fail(ErrorCode::invalidPayload,
                                "velocity[" + position + "] must be [mean, deviation]");
                style.velocity[std::atoi(position.c_str())] = {(*items)[0].asDouble().value(),
                                                               (*items)[1].asDouble().value()};
            }
        }
    }

    return model;
}

Value StyleModel::toValue() const
{
    Value::Object roles;
    for (std::size_t r = 0; r < roleNames.size(); ++r)
    {
        const auto& style = roles_[r];
        Value::Object members;
        for (std::size_t t = 0; t < tableNames.size(); ++t)
            members.emplace_back(std::string{tableNames[t]}, tableToValue(tableOf(style, t)));

        Value::Object velocity;
        for (const auto& [position, pair] : style.velocity)
            velocity.emplace_back(std::to_string(position),
                                  Value::array({Value{pair.first}, Value{pair.second}}));
        members.emplace_back("velocity", Value::object(std::move(velocity)));

        roles.emplace_back(std::string{roleNames[r]}, Value::object(std::move(members)));
    }

    return Value::object({{"format", Value{format}},
                          {"version", Value{version}},
                          {"origin", Value{origin_}},
                          {"roles", Value::object(std::move(roles))}});
}

// --- contexts ------------------------------------------------------------------

std::vector<std::string> rhythmContexts(int position, const std::vector<int>& intervals)
{
    auto out = suffixes("p" + std::to_string(position) + "|", intervals);
    out.emplace_back("|");
    return out;
}

std::vector<std::string> durationContexts(int interval)
{
    return {std::to_string(interval)};
}

std::vector<std::string> intervalContexts(const std::vector<int>& intervals)
{
    return suffixes("", intervals);
}

std::vector<std::string> degreeContexts(bool strong)
{
    return {strong ? "s" : "w"};
}

std::vector<std::string> progressionContexts(const std::vector<int>& roots)
{
    return suffixes("", roots);
}

double smoothed(const Table& table, const std::vector<std::string>& contexts, int x, std::size_t candidates)
{
    return smoothed(table, contexts, std::vector<int>{x}, candidates).front();
}

std::vector<double> smoothed(const Table& table,
                             const std::vector<std::string>& contexts,
                             const std::vector<int>& candidates,
                             std::size_t choices)
{
    std::vector<double> probability(candidates.size(),
                                    1.0 / static_cast<double>(std::max<std::size_t>(choices, 1)));

    // Shortest first: each longer context refines what the shorter one said.
    for (auto it = contexts.rbegin(); it != contexts.rend(); ++it)
    {
        const auto found = table.find(*it);
        if (found == table.end())
            continue;

        double total = 0.0;
        for (const auto& [value, count] : found->second)
            total += count;

        for (std::size_t i = 0; i < candidates.size(); ++i)
        {
            const auto hit = found->second.find(candidates[i]);
            const auto count = hit != found->second.end() ? hit->second : 0.0;
            probability[i] = (count + alpha * probability[i]) / (total + alpha);
        }
    }

    return probability;
}

} // namespace daw::domain::generation
