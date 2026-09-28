#include "daw/domain/generation/Transform.h"

#include "daw/domain/generation/Harmony.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>

namespace daw::domain::generation
{
namespace
{

constexpr double epsilon = 1e-6;
constexpr int drawsPerTransform = Variants::maxVariants;
constexpr int variationVelocity = 15;
constexpr double humanTimingBeats = 1.0 / 64.0;
constexpr int humanVelocity = 8;
constexpr int lowestVelocity = 1;
constexpr int highestVelocity = 127;

// The same small generator as the rest of generation: written here, so the
// same seed gives the same draw on every standard library.
class Draw
{
public:
    explicit Draw(std::uint64_t seed)
        : state_{seed}
    {
    }

    std::uint64_t next() noexcept
    {
        state_ += 0x9E3779B97F4A7C15ULL;
        auto z = state_;
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    // In [0, count).
    int below(int count) noexcept
    {
        return count <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(count));
    }

    // In [-1, 1].
    double unit() noexcept
    {
        return static_cast<double>(next() >> 11U) / static_cast<double>(1ULL << 53U) * 2.0 - 1.0;
    }

private:
    std::uint64_t state_;
};

[[nodiscard]] std::uint64_t seedOf(const std::vector<GhostNote>& source, Transform transform, int variant)
{
    std::uint64_t seed = 1469598103934665603ULL ^ static_cast<std::uint64_t>(transform);
    const auto mix = [&seed](std::uint64_t value)
    {
        seed ^= value;
        seed *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(variant));
    for (const auto& note : source)
    {
        mix(static_cast<std::uint64_t>(note.pitch));
        mix(static_cast<std::uint64_t>(std::llround(note.startBeats * 960.0)));
    }
    return seed;
}

void sortByTime(std::vector<GhostNote>& notes)
{
    std::stable_sort(notes.begin(),
                     notes.end(),
                     [](const GhostNote& lhs, const GhostNote& rhs)
                     {
                         if (std::abs(lhs.startBeats - rhs.startBeats) > epsilon)
                             return lhs.startBeats < rhs.startBeats;
                         return lhs.pitch < rhs.pitch;
                     });
}

// Notes that start together: a chord is one attack.
[[nodiscard]] std::vector<std::vector<GhostNote>> groupsOf(std::vector<GhostNote> notes)
{
    sortByTime(notes);
    std::vector<std::vector<GhostNote>> out;
    for (const auto& note : notes)
    {
        if (out.empty() || std::abs(out.back().front().startBeats - note.startBeats) > epsilon)
            out.emplace_back();
        out.back().push_back(note);
    }
    return out;
}

[[nodiscard]] std::vector<GhostNote> flatten(const std::vector<std::vector<GhostNote>>& groups)
{
    std::vector<GhostNote> out;
    for (const auto& group : groups)
        out.insert(out.end(), group.begin(), group.end());
    sortByTime(out);
    return out;
}

[[nodiscard]] double stepOf(const ResolvedConstraints& constraints)
{
    return stepsOf(constraints.resolution.value) * stepBeats;
}

// --- keep the rhythm -------------------------------------------------------------

[[nodiscard]] std::vector<GhostNote> keepRhythm(const std::vector<GhostNote>& source,
                                                const Context& context,
                                                const ResolvedConstraints& constraints,
                                                const StyleModel& model,
                                                int variant)
{
    const auto sourceGroups = groupsOf(source);
    std::vector<GhostNote> best;
    std::size_t bestChanged = 0;

    // The generator draws the zone; its pitches, in order, go onto the
    // source's attacks. A draw that changes less than half the notes is not a
    // new melody: the next one is tried.
    for (int draw = 0; draw < drawsPerTransform; ++draw)
    {
        const auto drawn =
            groupsOf(generate(context, constraints, model, variant * drawsPerTransform + draw));
        if (drawn.empty())
            continue;

        auto groups = sourceGroups;
        std::size_t changed = 0;
        for (std::size_t index = 0; index < groups.size(); ++index)
        {
            auto pitches = std::vector<int>{};
            for (const auto& note : drawn[index % drawn.size()])
                pitches.push_back(note.pitch);
            std::sort(pitches.begin(), pitches.end());

            auto& group = groups[index];
            std::sort(group.begin(),
                      group.end(),
                      [](const GhostNote& lhs, const GhostNote& rhs) { return lhs.pitch < rhs.pitch; });
            for (std::size_t voice = 0; voice < group.size(); ++voice)
            {
                const auto pitch = pitches[voice % pitches.size()];
                if (pitch != group[voice].pitch)
                    ++changed;
                group[voice].pitch = pitch;
            }
        }

        if (best.empty() || changed > bestChanged)
        {
            best = flatten(groups);
            bestChanged = changed;
        }
        if (changed * 2 >= source.size())
            break;
    }
    return best;
}

// --- keep the pitches ------------------------------------------------------------

[[nodiscard]] std::vector<GhostNote> keepPitches(const std::vector<GhostNote>& source,
                                                 const Context& context,
                                                 const ResolvedConstraints& constraints,
                                                 const StyleModel& model,
                                                 int variant)
{
    const auto sourceGroups = groupsOf(source);
    const auto count = sourceGroups.size();

    const auto onsetsOf = [](const std::vector<std::vector<GhostNote>>& groups)
    {
        std::vector<long long> out;
        for (const auto& group : groups)
            out.push_back(std::llround(group.front().startBeats * 960.0));
        return out;
    };
    const auto sourceOnsets = onsetsOf(sourceGroups);

    // A drawn rhythm with at least as many attacks as there are notes; the
    // attacks kept are spread over it, the first one always.
    auto denser = constraints;
    for (int draw = 0; draw < drawsPerTransform * 2; ++draw)
    {
        if (draw == drawsPerTransform)
            denser.density = {Density::dense, Source::imposed};

        const auto drawn = groupsOf(generate(context, denser, model, variant * drawsPerTransform + draw));
        if (drawn.size() < count)
            continue;

        std::vector<std::vector<GhostNote>> groups;
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto taken = index * drawn.size() / count;
            const auto start = drawn[taken].front().startBeats;
            const auto nextStart = index + 1 < count
                                       ? drawn[(index + 1) * drawn.size() / count].front().startBeats
                                       : context.toBeats;
            const auto length =
                std::max(stepBeats, std::min(drawn[taken].front().lengthBeats, nextStart - start));

            auto group = sourceGroups[index];
            for (auto& note : group)
            {
                note.startBeats = start;
                note.lengthBeats = length;
            }
            groups.push_back(std::move(group));
        }

        if (onsetsOf(groups) != sourceOnsets)
            return flatten(groups);
    }

    // No drawn rhythm fits: every other attack pushed off by one step.
    auto groups = sourceGroups;
    const auto step = stepOf(constraints);
    for (std::size_t index = 1; index < groups.size(); index += 2)
    {
        for (auto& note : groups[index])
        {
            if (note.startBeats + step < context.toBeats - epsilon)
                note.startBeats += step;
        }
    }
    return flatten(groups);
}

// --- one light variation -----------------------------------------------------------

[[nodiscard]] std::vector<GhostNote> variation(const std::vector<GhostNote>& source,
                                               const Context& context,
                                               const ResolvedConstraints& constraints,
                                               int variant)
{
    auto notes = source;
    sortByTime(notes);
    Draw draw{seedOf(source, Transform::variation, variant)};
    const auto step = stepOf(constraints);
    const auto pitched = constraints.role.value != Role::rhythm;

    const auto startsAt = [&notes](double beats)
    {
        return std::any_of(notes.begin(),
                           notes.end(),
                           [beats](const GhostNote& note)
                           { return std::abs(note.startBeats - beats) < epsilon; });
    };

    // Three kinds, tried from the one the draw picks; the first that can be
    // made is made, and it is the only change.
    const auto first = draw.below(3);
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        switch ((first + attempt) % 3)
        {
        case 0: // a note moved by one step, never the first
        {
            if (notes.size() < 2)
                break;
            const auto count = static_cast<int>(notes.size()) - 1;
            const auto offset = draw.below(count);
            for (int tried = 0; tried < count; ++tried)
            {
                auto& note = notes[static_cast<std::size_t>(1 + (offset + tried) % count)];
                for (const auto direction : {1.0, -1.0})
                {
                    const auto moved = note.startBeats + direction * step;
                    if (moved >= context.fromBeats - epsilon &&
                        moved + stepBeats <= context.toBeats + epsilon && !startsAt(moved))
                    {
                        note.startBeats = moved;
                        note.lengthBeats = std::min(note.lengthBeats, context.toBeats - moved);
                        sortByTime(notes);
                        return notes;
                    }
                }
            }
            break;
        }
        case 1: // a velocity changed
        {
            auto& note = notes[static_cast<std::size_t>(draw.below(static_cast<int>(notes.size())))];
            const auto up = std::min(highestVelocity, note.velocity + variationVelocity);
            const auto down = std::max(lowestVelocity, note.velocity - variationVelocity);
            note.velocity = up != note.velocity ? up : down;
            return notes;
        }
        default: // the last note closed on the tonic, or opened on the fifth
        {
            if (!pitched)
                break;
            auto& last = notes.back();
            const auto key = constraints.key.value;
            const auto tonic = ((last.pitch - key.tonic) % 12 + 12) % 12 == 0;
            const auto target = tonic ? (key.tonic + 7) % 12 : key.tonic;
            int best = last.pitch;
            for (int distance = 1; distance <= 6 && best == last.pitch; ++distance)
            {
                for (const auto candidate : {last.pitch - distance, last.pitch + distance})
                {
                    if (((candidate - target) % 12 + 12) % 12 == 0)
                    {
                        best = candidate;
                        break;
                    }
                }
            }
            if (best != last.pitch)
            {
                last.pitch = best;
                return notes;
            }
            break;
        }
        }
    }
    return notes;
}

// --- darker, brighter --------------------------------------------------------------

[[nodiscard]] int darkened(int pitch, Key key)
{
    if (key.mode == Mode::major)
    {
        // The major degrees with a minor counterpart (3, 6, 7) go down a
        // semitone; the others are the same in both.
        const Key minor{key.tonic, Mode::minor};
        return inScale(pitch, minor) ? pitch : pitch - 1;
    }
    const auto index = diatonicIndex(pitch, key);
    return index.has_value() ? pitchOfIndex(*index - 1, key) : pitch;
}

[[nodiscard]] int brightened(int pitch, Key key)
{
    if (key.mode == Mode::minor)
    {
        const Key major{key.tonic, Mode::major};
        return inScale(pitch, major) ? pitch : pitch + 1;
    }
    const auto index = diatonicIndex(pitch, key);
    return index.has_value() ? pitchOfIndex(*index + 1, key) : pitch;
}

[[nodiscard]] std::vector<GhostNote> recoloured(const std::vector<GhostNote>& source, Key key, bool darker)
{
    auto notes = source;
    for (auto& note : notes)
    {
        // Out of the key: the person's own note, left alone.
        if (!inScale(note.pitch, key))
            continue;
        note.pitch = std::clamp(darker ? darkened(note.pitch, key) : brightened(note.pitch, key), 0, 127);
    }
    sortByTime(notes);
    return notes;
}

// --- busier, calmer --------------------------------------------------------------------

[[nodiscard]] std::vector<GhostNote> busier(const std::vector<GhostNote>& source,
                                            const ResolvedConstraints& constraints)
{
    const auto step = std::min(stepOf(constraints), stepBeats * 2.0);
    std::vector<GhostNote> out;
    for (const auto& note : source)
    {
        // Long enough to be heard twice: two attacks at the same pitch, the
        // second a little softer.
        if (note.lengthBeats >= step * 2.0 - epsilon)
        {
            const auto half =
                std::max(step, std::floor(note.lengthBeats / 2.0 / stepBeats + epsilon) * stepBeats);
            auto first = note;
            first.lengthBeats = half;
            auto second = note;
            second.startBeats = note.startBeats + half;
            second.lengthBeats = note.lengthBeats - half;
            second.velocity = std::max(lowestVelocity, note.velocity - 10);
            out.push_back(first);
            out.push_back(second);
            continue;
        }
        out.push_back(note);
    }
    sortByTime(out);
    return out;
}

[[nodiscard]] std::vector<GhostNote> calmer(const std::vector<GhostNote>& source)
{
    auto groups = groupsOf(source);
    if (groups.size() < 2)
        return flatten(groups);

    const auto onBeat = [](const std::vector<GhostNote>& group)
    {
        const auto start = group.front().startBeats;
        return std::abs(start - std::round(start)) < epsilon;
    };
    const auto anyOff = std::any_of(
        groups.begin() + 1, groups.end(), [&onBeat](const auto& group) { return !onBeat(group); });

    std::vector<std::vector<GhostNote>> kept{groups.front()};
    for (std::size_t index = 1; index < groups.size(); ++index)
    {
        const auto keep = anyOff ? onBeat(groups[index]) : index % 2 == 0;
        if (keep)
        {
            kept.push_back(groups[index]);
            continue;
        }
        // The one before is held through the one that left.
        const auto end = groups[index].front().startBeats + groups[index].front().lengthBeats;
        for (auto& note : kept.back())
            note.lengthBeats = std::max(note.lengthBeats, end - note.startBeats);
    }
    return flatten(kept);
}

// --- humanize ----------------------------------------------------------------------

[[nodiscard]] std::vector<GhostNote>
humanized(const std::vector<GhostNote>& source, const Context& context, int variant)
{
    auto notes = source;
    sortByTime(notes);
    Draw draw{seedOf(source, Transform::humanize, variant)};
    for (auto& note : notes)
    {
        auto moved = note.startBeats + draw.unit() * humanTimingBeats;
        moved = std::clamp(moved, context.fromBeats, context.toBeats - stepBeats / 4.0);
        note.startBeats = moved;
        note.velocity = std::clamp(note.velocity + static_cast<int>(std::lround(draw.unit() * humanVelocity)),
                                   lowestVelocity,
                                   highestVelocity);
    }
    // The order of the attacks is the person's: a jitter never swaps two.
    for (std::size_t index = 1; index < notes.size(); ++index)
        notes[index].startBeats = std::max(notes[index].startBeats, notes[index - 1].startBeats);
    return notes;
}

// --- reading ---------------------------------------------------------------------------

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string out{text};
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

struct Phrase
{
    Transform transform;
    std::array<std::string_view, 6> phrases;
};

// In order of precedence: "garde le rythme, plus sombre" is darker, which
// keeps the rhythm anyway.
constexpr std::array<Phrase, 8> phrasesByTransform{{
    {Transform::humanize, {"humanise", "humaine", "humain", "moins carré", "", ""}},
    {Transform::darker, {"plus sombre", "sombre", "plus triste", "triste", "plus grave", ""}},
    {Transform::brighter, {"plus clair", "plus lumineux", "lumineux", "plus joyeux", "joyeux", "clair"}},
    {Transform::busier,
     {"plus rythmé", "rythmé", "plus de notes", "plus énergique", "énergique", "plus dense"}},
    {Transform::calmer, {"plus calme", "calme", "plus simple", "simplifie", "épure", "moins de notes"}},
    {Transform::variation, {"variante", "varie", "légère", "léger", "retouche", ""}},
    {Transform::keepPitches,
     {"garde les notes",
      "garde les hauteurs",
      "mêmes notes",
      "mêmes hauteurs",
      "change le rythme",
      "autre rythme"}},
    {Transform::keepRhythm,
     {"garde le rythme",
      "même rythme",
      "change les notes",
      "autres notes",
      "change les hauteurs",
      "autre mélodie"}},
}};

} // namespace

std::string_view describe(Transform transform) noexcept
{
    switch (transform)
    {
    case Transform::keepRhythm:
        return "même rythme, d'autres notes";
    case Transform::keepPitches:
        return "mêmes notes, un autre rythme";
    case Transform::variation:
        return "une variante légère";
    case Transform::darker:
        return "même rythme, notes plus sombres";
    case Transform::brighter:
        return "même rythme, notes plus claires";
    case Transform::busier:
        return "mêmes notes, plus rythmé";
    case Transform::calmer:
        return "mêmes notes, plus posé";
    case Transform::humanize:
        return "mêmes notes, jouées moins carré";
    }
    return "même rythme, d'autres notes";
}

std::string_view nameOf(Transform transform) noexcept
{
    switch (transform)
    {
    case Transform::keepRhythm:
        return "keep_rhythm";
    case Transform::keepPitches:
        return "keep_pitches";
    case Transform::variation:
        return "variation";
    case Transform::darker:
        return "darker";
    case Transform::brighter:
        return "brighter";
    case Transform::busier:
        return "busier";
    case Transform::calmer:
        return "calmer";
    case Transform::humanize:
        return "humanize";
    }
    return "keep_rhythm";
}

std::optional<Transform> transformNamed(std::string_view name) noexcept
{
    for (const auto transform : {Transform::keepRhythm,
                                 Transform::keepPitches,
                                 Transform::variation,
                                 Transform::darker,
                                 Transform::brighter,
                                 Transform::busier,
                                 Transform::calmer,
                                 Transform::humanize})
    {
        if (nameOf(transform) == name)
            return transform;
    }
    return std::nullopt;
}

std::optional<TransformReading> readTransform(std::string_view text)
{
    const auto said = lowered(text);
    for (const auto& entry : phrasesByTransform)
    {
        for (const auto phrase : entry.phrases)
        {
            if (phrase.empty() || said.find(phrase) == std::string::npos)
                continue;

            TransformReading out{entry.transform, {}};
            std::string word;
            for (const auto c : std::string{phrase} + " ")
            {
                if (c == ' ')
                {
                    if (!word.empty())
                        out.words.push_back(word);
                    word.clear();
                }
                else
                {
                    word.push_back(c);
                }
            }
            return out;
        }
    }
    return std::nullopt;
}

std::vector<GhostNote> sourceOf(const Context& context)
{
    std::vector<GhostNote> out;
    for (const auto& note : context.row)
    {
        if (note.startBeats < context.fromBeats - epsilon || note.startBeats >= context.toBeats - epsilon)
            continue;
        out.push_back(GhostNote{note.pitch, note.velocity, note.startBeats, note.lengthBeats});
    }
    sortByTime(out);
    return out;
}

std::vector<GhostNote> transform(Transform transform,
                                 const std::vector<GhostNote>& source,
                                 const Context& context,
                                 const ResolvedConstraints& constraints,
                                 const StyleModel& model,
                                 int variant)
{
    if (source.empty())
        return {};

    switch (transform)
    {
    case Transform::keepRhythm:
        return keepRhythm(source, context, constraints, model, variant);
    case Transform::keepPitches:
        return keepPitches(source, context, constraints, model, variant);
    case Transform::variation:
        return variation(source, context, constraints, variant);
    case Transform::darker:
        return recoloured(source, constraints.key.value, true);
    case Transform::brighter:
        return recoloured(source, constraints.key.value, false);
    case Transform::busier:
        return busier(source, constraints);
    case Transform::calmer:
        return calmer(source);
    case Transform::humanize:
        return humanized(source, context, variant);
    }
    return source;
}

} // namespace daw::domain::generation
