#include "daw/domain/generation/Learning.h"

#include "daw/domain/command/CommandEnvelope.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <utility>

namespace daw::domain::generation
{
namespace
{

constexpr int maxGap = 16;
constexpr int maxInterval = 9;
constexpr double epsilon = 1e-6;

constexpr std::array<std::string_view, 4> roleNames{"melody", "bass", "chords", "rhythm"};

// Python rounds half to even, and the counts must be the same numbers.
[[nodiscard]] int toSteps(double beats)
{
    return static_cast<int>(std::nearbyint(beats * stepsPerBeat));
}

void keepLast(std::vector<int>& history, int value)
{
    history.push_back(value);
    if (history.size() > 3)
        history.erase(history.begin());
}

void count(Table& table, const std::vector<std::string>& contexts, int x, double weight = 1.0)
{
    for (const auto& context : contexts)
        table[context][x] += weight;
}

void addVelocity(RoleCounts& counts, int position, int velocity)
{
    auto& moments = counts.velocity[position];
    moments.count += 1.0;
    moments.sum += velocity;
    moments.squares += static_cast<double>(velocity) * velocity;
}

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string out{text};
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

[[nodiscard]] bool mentions(const std::string& name, std::initializer_list<std::string_view> words)
{
    return std::any_of(words.begin(),
                       words.end(),
                       [&name](std::string_view word) { return name.find(word) != std::string::npos; });
}

[[nodiscard]] std::vector<LineEvent> lineOf(const std::vector<Note>& notes, Role role)
{
    std::map<int, const Note*> chosen;
    for (const auto& note : notes)
    {
        const auto step = toSteps(note.startBeats);
        auto& slot = chosen[step];
        if (slot == nullptr || (role == Role::bass ? note.pitch < slot->pitch : note.pitch > slot->pitch))
            slot = &note;
    }

    std::vector<LineEvent> out;
    for (const auto& [step, note] : chosen)
        out.push_back({step, std::max(1, toSteps(note->lengthBeats)), note->pitch, note->velocity});
    return out;
}

[[nodiscard]] bool excludedNote(const Note& note, const std::map<std::string, Note>& excluded)
{
    const auto found = excluded.find(note.id.toString());
    if (found == excluded.end())
        return false;
    const auto& written = found->second;
    return written.pitch == note.pitch && written.velocity == note.velocity &&
           std::abs(written.startBeats - note.startBeats) < epsilon &&
           std::abs(written.lengthBeats - note.lengthBeats) < epsilon;
}

// --- Value forms -------------------------------------------------------------

[[nodiscard]] Value momentsToValue(const std::map<int, Moments>& velocity)
{
    Value::Object out;
    for (const auto& [position, moments] : velocity)
        out.emplace_back(std::to_string(position),
                         Value::array({Value{moments.count}, Value{moments.sum}, Value{moments.squares}}));
    return Value::object(std::move(out));
}

[[nodiscard]] Result<std::map<int, Moments>> momentsFromValue(const Value* value)
{
    std::map<int, Moments> out;
    if (value == nullptr || value->isNull())
        return out;
    const auto* positions = value->asObject();
    if (positions == nullptr)
        return fail(ErrorCode::invalidPayload, "learned velocity must be an object");

    for (const auto& [position, triple] : *positions)
    {
        const auto* items = triple.asArray();
        if (items == nullptr || items->size() != 3 || !(*items)[0].asDouble() || !(*items)[1].asDouble() ||
            !(*items)[2].asDouble())
            return fail(ErrorCode::invalidPayload,
                        "learned velocity[" + position + "] must be [n, sum, squares]");
        out[std::atoi(position.c_str())] = Moments{
            (*items)[0].asDouble().value(), (*items)[1].asDouble().value(), (*items)[2].asDouble().value()};
    }
    return out;
}

// --- the prior ---------------------------------------------------------------

enum class Kind
{
    rhythm,
    duration,
    interval,
    degree,
    progression
};

// The contexts a context backs off through, longest first: what the
// generator would ask the table for, had it reached this context.
[[nodiscard]] std::vector<std::string> backoffOf(Kind kind, const std::string& context)
{
    const auto historyOf = [](std::string_view text)
    {
        std::vector<int> out;
        std::size_t at = 0;
        while (at < text.size())
        {
            auto end = text.find(',', at);
            if (end == std::string_view::npos)
                end = text.size();
            out.push_back(std::atoi(std::string{text.substr(at, end - at)}.c_str()));
            at = end + 1;
        }
        return out;
    };

    switch (kind)
    {
    case Kind::rhythm:
    {
        const auto bar = context.find('|');
        if (bar == std::string::npos || bar == 0)
            return {context};
        return rhythmContexts(std::atoi(context.c_str() + 1),
                              historyOf(std::string_view{context}.substr(bar + 1)));
    }
    case Kind::interval:
        return intervalContexts(historyOf(context));
    case Kind::progression:
        return progressionContexts(historyOf(context));
    case Kind::duration:
    case Kind::degree:
        break;
    }
    return {context};
}

[[nodiscard]] std::vector<int> domainOf(Kind kind, const std::string& context)
{
    std::vector<int> out;
    const auto range = [&out](int from, int to)
    {
        for (int x = from; x <= to; ++x)
            out.push_back(x);
    };
    switch (kind)
    {
    case Kind::rhythm:
        range(1, maxGap);
        break;
    case Kind::duration:
        range(1, std::clamp(std::atoi(context.c_str()), 1, maxGap));
        break;
    case Kind::interval:
        range(-maxInterval, maxInterval);
        break;
    case Kind::degree:
    case Kind::progression:
        range(0, 6);
        break;
    }
    return out;
}

// Every context either side knows. Where the person played, the base comes in
// as a prior whose weight is `mass / notes` times what they played there: the
// learned share is then notes / (notes + mass) at every context, the number
// the zone shows. Where they never played, the base alone, as it was.
[[nodiscard]] Table blended(Kind kind, const Table& base, const Table& learned, double mass, double notes)
{
    Table out = base;
    for (const auto& [context, counts] : learned)
    {
        double seen = 0.0;
        for (const auto& [x, n] : counts)
            seen += n;
        if (seen <= 0.0)
            continue;

        const auto domain = domainOf(kind, context);
        const auto prior = smoothed(base, backoffOf(kind, context), domain, domain.size());
        double total = 0.0;
        for (const auto p : prior)
            total += p;

        auto& target = out[context];
        target.clear();
        const auto weight = mass / notes * seen;
        for (std::size_t i = 0; i < domain.size(); ++i)
        {
            if (total > 0.0)
                target[domain[i]] += weight * prior[i] / total;
        }
        for (const auto& [x, n] : counts)
            target[x] += n;
    }
    return out;
}

} // namespace

// --- RoleCounts / Learned ----------------------------------------------------------

std::map<int, std::pair<double, double>> RoleCounts::velocityStyle() const
{
    std::map<int, std::pair<double, double>> out;
    for (const auto& [position, moments] : velocity)
    {
        if (moments.count <= 0.0)
            continue;
        const auto mean = moments.sum / moments.count;
        auto deviation = 8.0;
        if (moments.count > 1.0)
            deviation = std::sqrt(std::max(0.0, moments.squares / moments.count - mean * mean));
        out[position] = {mean, std::max(deviation, 2.0)};
    }
    return out;
}

const RoleCounts& Learned::role(Role role) const noexcept
{
    return roles[static_cast<std::size_t>(role)];
}

RoleCounts& Learned::role(Role role) noexcept
{
    return roles[static_cast<std::size_t>(role)];
}

double Learned::notes() const noexcept
{
    double total = 0.0;
    for (const auto& counts : roles)
        total += counts.notes;
    return total;
}

void Learned::add(const Learned& other, double weight)
{
    for (std::size_t r = 0; r < roles.size(); ++r)
    {
        auto& mine = roles[r];
        const auto& theirs = other.roles[r];
        for (auto [target, source] : {std::pair{&mine.tables.rhythm, &theirs.tables.rhythm},
                                      std::pair{&mine.tables.duration, &theirs.tables.duration},
                                      std::pair{&mine.tables.interval, &theirs.tables.interval},
                                      std::pair{&mine.tables.degree, &theirs.tables.degree},
                                      std::pair{&mine.tables.progression, &theirs.tables.progression}})
        {
            for (const auto& [context, counts] : *source)
            {
                auto& into = (*target)[context];
                for (const auto& [x, n] : counts)
                    into[x] += n * weight;
            }
        }
        for (const auto& [position, moments] : theirs.velocity)
        {
            auto& into = mine.velocity[position];
            into.count += moments.count * weight;
            into.sum += moments.sum * weight;
            into.squares += moments.squares * weight;
        }
        mine.notes += theirs.notes * weight;
    }
}

Value Learned::toValue() const
{
    // The tables in the style model's own form, so the two files read alike.
    std::array<RoleStyle, 4> tables{};
    for (std::size_t r = 0; r < roles.size(); ++r)
        tables[r] = roles[r].tables;
    const auto asStyle = StyleModel::of(tables, "appris").toValue();

    Value::Object counts;
    for (std::size_t r = 0; r < roles.size(); ++r)
    {
        counts.emplace_back(std::string{roleNames[r]},
                            Value::object({{"notes", Value{roles[r].notes}},
                                           {"velocity", momentsToValue(roles[r].velocity)}}));
    }

    return Value::object({{"format", Value{format}},
                          {"version", Value{version}},
                          {"style", asStyle},
                          {"counts", Value::object(std::move(counts))}});
}

Result<Learned> Learned::fromValue(const Value& value)
{
    auto declared = value.stringAt("format");
    if (!declared || declared.value() != format)
        return fail(ErrorCode::invalidPayload, "not a learned style: format must be \"daw-ia.learned\"");
    auto declaredVersion = value.intAt("version");
    if (!declaredVersion || declaredVersion.value() != version)
        return fail(ErrorCode::invalidPayload, "learned style: unsupported version");

    const auto* style = value.find("style");
    if (style == nullptr)
        return fail(ErrorCode::invalidPayload, "learned style: no tables");
    auto tables = StyleModel::fromValue(*style);
    if (!tables)
        return tables.error();

    Learned out{};
    const auto* counts = value.find("counts");
    for (std::size_t r = 0; r < roleNames.size(); ++r)
    {
        auto& role = out.roles[r];
        role.tables = tables.value().role(static_cast<Role>(r));
        role.tables.velocity.clear();

        const auto* entry = counts != nullptr ? counts->find(roleNames[r]) : nullptr;
        if (entry == nullptr)
            continue;
        if (const auto* notes = entry->find("notes"); notes != nullptr && notes->asDouble())
            role.notes = notes->asDouble().value();
        auto velocity = momentsFromValue(entry->find("velocity"));
        if (!velocity)
            return velocity.error();
        role.velocity = std::move(velocity).value();
    }
    return out;
}

// --- counting ------------------------------------------------------------------------

void countLine(RoleCounts& counts, const std::vector<LineEvent>& events, Key key, int barSteps, Role role)
{
    std::vector<int> gaps;
    std::vector<int> intervals;
    std::optional<int> previous;

    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const auto& event = events[i];
        const auto position = event.step % barSteps % 16;
        addVelocity(counts, position, event.velocity);
        counts.notes += 1.0;

        if (i + 1 < events.size())
        {
            const auto gap = events[i + 1].step - event.step;
            if (gap > maxGap)
            {
                // A long rest ends the phrase: what comes after does not follow from it.
                gaps.clear();
            }
            else
            {
                count(counts.tables.rhythm, rhythmContexts(position, gaps), gap);
                counts.tables.duration[std::to_string(gap)][std::min(event.length, gap)] += 1.0;
                keepLast(gaps, gap);
            }
        }

        if (role == Role::rhythm)
            continue;

        const auto index = degreeIndex(event.pitch, key);
        if (!index.has_value())
            continue;

        counts.tables.degree[position % stepsPerBeat == 0 ? "s" : "w"][*index % 7] += 1.0;
        if (previous.has_value())
        {
            const auto step = std::clamp(*index - *previous, -maxInterval, maxInterval);
            count(counts.tables.interval, intervalContexts(intervals), step);
            keepLast(intervals, step);
        }
        previous = index;
    }
}

void countChords(RoleCounts& counts, const std::vector<Note>& notes, Key key, int barSteps)
{
    if (notes.empty())
        return;

    std::map<int, int> velocities; // the last note at a step gives the velocity, as in corpus.py
    for (const auto& note : notes)
        velocities[toSteps(note.startBeats)] = note.velocity;

    std::vector<int> onsets;
    for (const auto& [step, velocity] : velocities)
    {
        onsets.push_back(step);
        addVelocity(counts, step % barSteps % 16, velocity);
        counts.notes += 1.0;
    }

    std::vector<int> gaps;
    for (std::size_t i = 0; i + 1 < onsets.size(); ++i)
    {
        const auto gap = onsets[i + 1] - onsets[i];
        const auto position = onsets[i] % barSteps % 16;
        if (gap > maxGap)
        {
            gaps.clear();
            continue;
        }
        count(counts.tables.rhythm, rhythmContexts(position, gaps), gap);
        counts.tables.duration[std::to_string(gap)][gap] += 1.0;
        keepLast(gaps, gap);
    }

    // One chord per bar, read with the generator's own rule.
    const auto barBeats = static_cast<double>(barSteps) / stepsPerBeat;
    double lastEnd = 0.0;
    for (const auto& note : notes)
        lastEnd = std::max(lastEnd, note.startBeats + note.lengthBeats);

    std::vector<int> roots;
    for (int bar = 0; bar < lastEnd / barBeats; ++bar)
    {
        const auto start = bar * barBeats;
        const auto end = start + barBeats;
        std::vector<WeightedPitch> heard;
        for (const auto& note : notes)
        {
            const auto overlap =
                std::min(end, note.startBeats + note.lengthBeats) - std::max(start, note.startBeats);
            if (overlap > 0.0)
                heard.push_back({note.pitch, overlap});
        }
        if (const auto chord = detectChord(heard, key); chord.has_value())
        {
            count(counts.tables.progression, progressionContexts(roots), chord->root);
            keepLast(roots, chord->root);
        }
    }
}

Role roleOfRow(const Track& track, const std::vector<Note>& notes)
{
    const auto onePitch = std::all_of(
        notes.begin(), notes.end(), [&notes](const Note& note) { return note.pitch == notes.front().pitch; });
    if (track.sample.has_value() || onePitch)
        return Role::rhythm;

    const auto name = lowered(track.name);
    if (mentions(name, {"808", "bass", "basse", "sub"}))
        return Role::bass;
    if (mentions(name, {"chord", "accord", "pad", "nappe"}))
        return Role::chords;
    if (mentions(name, {"lead", "melod", "mélod", "pluck", "bell"}))
        return Role::melody;

    std::map<int, int> starts;
    for (const auto& note : notes)
        ++starts[toSteps(note.startBeats)];
    for (const auto& [step, many] : starts)
    {
        if (many >= 3)
            return Role::chords;
    }

    std::vector<int> pitches;
    for (const auto& note : notes)
        pitches.push_back(note.pitch);
    std::sort(pitches.begin(), pitches.end());
    return pitches[pitches.size() / 2] < 48 ? Role::bass : Role::melody;
}

Learned learn(const ProjectState& state, const std::map<std::string, Note>& excluded)
{
    struct Row
    {
        Role role;
        std::vector<Note> notes;
    };

    // Every row of every pattern, with what the machine wrote left out.
    std::vector<std::vector<Row>> patterns;
    std::vector<WeightedPitch> everywhere;
    for (const auto& pattern : state.patterns())
    {
        std::vector<Row> rows;
        for (const auto& clip : pattern.clips)
        {
            const auto* track = state.findTrack(clip.trackId);
            if (track == nullptr)
                continue;
            std::vector<Note> notes;
            for (const auto& note : clip.notes)
            {
                if (!excludedNote(note, excluded))
                    notes.push_back(note);
            }
            if (notes.empty())
                continue;
            const auto role = roleOfRow(*track, notes);
            if (role != Role::rhythm)
            {
                for (const auto& note : notes)
                    everywhere.push_back({note.pitch, note.lengthBeats});
            }
            rows.push_back({role, std::move(notes)});
        }
        patterns.push_back(std::move(rows));
    }

    const auto projectKey = detectKey(everywhere).value_or(Key{9, Mode::minor});
    const auto barSteps = std::max(1, static_cast<int>(std::lround(state.beatsPerBar() * stepsPerBeat)));

    Learned out{};
    for (const auto& rows : patterns)
    {
        std::vector<WeightedPitch> pitched;
        for (const auto& row : rows)
        {
            if (row.role == Role::rhythm)
                continue;
            for (const auto& note : row.notes)
                pitched.push_back({note.pitch, note.lengthBeats});
        }
        const auto key = detectKey(pitched).value_or(projectKey);

        for (const auto& row : rows)
        {
            auto& counts = out.role(row.role);
            if (row.role == Role::chords)
                countChords(counts, row.notes, key, barSteps);
            else
                countLine(counts, lineOf(row.notes, row.role), key, barSteps, row.role);
        }
    }
    return out;
}

// --- MachineNotes ------------------------------------------------------------------

void MachineNotes::take(std::string_view type, const Provenance& origin, const Value& payload)
{
    if (type != "note.add" || origin.actor == Actor::user || payload.isNull())
        return;
    auto note = Note::fromValue(payload);
    if (note)
        notes_[note.value().id.toString()] = note.value();
}

void MachineNotes::read(const std::vector<Value>& journal)
{
    for (const auto& value : journal)
    {
        auto envelope = CommandEnvelope::fromValue(value);
        if (envelope)
            take(envelope.value().type, envelope.value().origin, envelope.value().payload);
    }
}

void MachineNotes::observe(const Receipt& receipt)
{
    take(receipt.type, receipt.origin, receipt.payload);
}

// --- blending ------------------------------------------------------------------------

double learnedShare(const Learned& learned, Role role, double base) noexcept
{
    const auto notes = learned.role(role).notes;
    return notes <= 0.0 ? 0.0 : notes / (notes + base);
}

StyleModel blend(const StyleModel& base, const Learned& learned, double baseMass)
{
    if (learned.notes() <= 0.0)
        return base;

    std::array<RoleStyle, 4> roles{};
    for (std::size_t r = 0; r < roles.size(); ++r)
    {
        const auto role = static_cast<Role>(r);
        const auto& from = base.role(role);
        const auto& counts = learned.role(role);
        auto& into = roles[r];
        if (counts.notes <= 0.0)
        {
            into = from;
            continue;
        }

        into.rhythm = blended(Kind::rhythm, from.rhythm, counts.tables.rhythm, baseMass, counts.notes);
        into.duration =
            blended(Kind::duration, from.duration, counts.tables.duration, baseMass, counts.notes);
        into.interval =
            blended(Kind::interval, from.interval, counts.tables.interval, baseMass, counts.notes);
        into.degree = blended(Kind::degree, from.degree, counts.tables.degree, baseMass, counts.notes);
        into.progression =
            blended(Kind::progression, from.progression, counts.tables.progression, baseMass, counts.notes);

        // The velocity: the base as that many notes at its mean and deviation.
        std::map<int, Moments> moments = counts.velocity;
        for (const auto& [position, pair] : from.velocity)
            static_cast<void>(moments[position]);
        for (const auto& [position, learnedMoments] : moments)
        {
            const auto found = from.velocity.find(position);
            const auto mean = found != from.velocity.end() ? found->second.first : 100.0;
            const auto deviation = found != from.velocity.end() ? found->second.second : 8.0;
            const auto n = baseMass + learnedMoments.count;
            const auto combined = (baseMass * mean + learnedMoments.sum) / n;
            const auto square =
                (baseMass * (deviation * deviation + mean * mean) + learnedMoments.squares) / n;
            into.velocity[position] = {combined,
                                       std::max(2.0, std::sqrt(std::max(0.0, square - combined * combined)))};
        }
    }
    return StyleModel::of(std::move(roles), "appris");
}

} // namespace daw::domain::generation
