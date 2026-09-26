#include "daw/domain/generation/Generator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace daw::domain::generation
{
namespace
{

constexpr double epsilon = 1e-6;

// Sharpens the style model's preferences before drawing: below one, likely
// candidates get likelier. One would copy the corpus's hesitations too.
constexpr double temperature = 0.75;

// A chord tone on a strong beat is what makes a line sound like it belongs to
// the chords under it. A preference, not a rule: passing notes stay legal.
constexpr double chordToneOnBeat = 2.0;
constexpr double chordToneOffBeat = 1.2;

// How much a density bends the rhythm: the weight of an interval is
// multiplied by the interval to this power, positive for sparse.
constexpr double densityBend = 0.8;

constexpr int maxInterval = 9;
constexpr int maxOnsetGap = 16;

// splitmix64: four lines, the same numbers on every compiler. std::mt19937 is
// portable, but std::uniform_real_distribution and friends are not.
class Random
{
public:
    explicit Random(std::uint64_t seed)
        : state_{seed}
    {
    }

    std::uint64_t next() noexcept
    {
        state_ += 0x9E3779B97F4A7C15ULL;
        auto z = state_;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    double uniform() noexcept { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

    double gaussian() noexcept
    {
        const auto u = std::max(uniform(), 1e-12);
        const auto v = uniform();
        return std::sqrt(-2.0 * std::log(u)) * std::cos(6.283185307179586 * v);
    }

    // An index drawn with the given weights, sharpened by the temperature.
    std::size_t pick(const std::vector<double>& weights) noexcept
    {
        double total = 0.0;
        std::vector<double> sharpened(weights.size());
        for (std::size_t i = 0; i < weights.size(); ++i)
        {
            sharpened[i] = weights[i] > 0.0 ? std::pow(weights[i], 1.0 / temperature) : 0.0;
            total += sharpened[i];
        }
        if (total <= 0.0)
            return 0;

        auto target = uniform() * total;
        for (std::size_t i = 0; i < sharpened.size(); ++i)
        {
            target -= sharpened[i];
            if (target < 0.0)
                return i;
        }
        return sharpened.size() - 1;
    }

private:
    std::uint64_t state_;
};

class Hasher
{
public:
    void add(std::int64_t value) noexcept
    {
        for (int byte = 0; byte < 8; ++byte)
        {
            hash_ ^= static_cast<std::uint64_t>((value >> (byte * 8)) & 0xFF);
            hash_ *= 0x100000001B3ULL;
        }
    }

    void add(double beats) noexcept { add(static_cast<std::int64_t>(std::llround(beats * 960.0))); }

    void add(const std::vector<Note>& notes) noexcept
    {
        add(static_cast<std::int64_t>(notes.size()));
        for (const auto& note : notes)
        {
            add(static_cast<std::int64_t>(note.pitch));
            add(static_cast<std::int64_t>(note.velocity));
            add(note.startBeats);
            add(note.lengthBeats);
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_{0xCBF29CE484222325ULL};
};

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

[[nodiscard]] std::vector<WeightedPitch> weighted(const std::vector<Note>& notes)
{
    std::vector<WeightedPitch> out;
    out.reserve(notes.size());
    for (const auto& note : notes)
        out.push_back({note.pitch, note.lengthBeats});
    return out;
}

[[nodiscard]] int medianPitch(std::vector<Note> notes)
{
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.pitch < b.pitch; });
    return notes[notes.size() / 2].pitch;
}

[[nodiscard]] int maxPolyphony(const std::vector<Note>& notes)
{
    int most = 0;
    for (const auto& note : notes)
    {
        const auto at = note.startBeats + epsilon;
        const auto sounding =
            std::count_if(notes.begin(),
                          notes.end(),
                          [at](const Note& other)
                          { return other.startBeats <= at && at < other.startBeats + other.lengthBeats; });
        most = std::max(most, static_cast<int>(sounding));
    }
    return most;
}

[[nodiscard]] Role deducedRole(const Context& context, Source& source)
{
    source = Source::deduced;
    if (context.sampleChannel)
        return Role::rhythm;

    const auto name = lowered(context.trackName);
    if (mentions(name, {"808", "bass", "basse", "sub"}))
        return Role::bass;
    if (mentions(name, {"chord", "accord", "pad", "nappe"}))
        return Role::chords;
    if (mentions(name, {"lead", "melod", "mélod", "pluck", "bell"}))
        return Role::melody;

    if (!context.row.empty())
    {
        if (maxPolyphony(context.row) >= 3)
            return Role::chords;
        if (medianPitch(context.row) < 48)
            return Role::bass;
        return Role::melody;
    }

    source = Source::defaulted;
    return Role::melody;
}

// The line a style model can read in a row: one pitch per onset, the highest
// for a melody and the lowest for a bass, in time order.
[[nodiscard]] std::vector<Note> lineOf(std::vector<Note> notes, Role role)
{
    std::sort(notes.begin(),
              notes.end(),
              [role](const Note& a, const Note& b)
              {
                  if (std::abs(a.startBeats - b.startBeats) > epsilon)
                      return a.startBeats < b.startBeats;
                  return role == Role::bass ? a.pitch < b.pitch : a.pitch > b.pitch;
              });

    std::vector<Note> line;
    for (const auto& note : notes)
    {
        if (line.empty() || std::abs(line.back().startBeats - note.startBeats) > epsilon)
            line.push_back(note);
    }
    return line;
}

[[nodiscard]] int toStep(double beats)
{
    return static_cast<int>(std::floor(beats / stepBeats + epsilon));
}

[[nodiscard]] int velocityAt(const RoleStyle& style, int position, Random& random)
{
    auto mean = 100.0;
    auto deviation = 8.0;
    if (const auto found = style.velocity.find(position % 16); found != style.velocity.end())
    {
        mean = found->second.first;
        deviation = found->second.second;
    }

    const auto drawn = static_cast<int>(std::lround(mean + deviation * random.gaussian()));
    return std::clamp(drawn, Note::lowestVelocity, Note::highestVelocity);
}

void keepLast(std::vector<int>& history, int value)
{
    history.push_back(value);
    if (history.size() > 3)
        history.erase(history.begin());
}

struct Grid
{
    int start{0};
    int end{0};
    int resolution{1};
    int bar{16};
};

[[nodiscard]] Grid gridOf(const Context& context, const ResolvedConstraints& constraints)
{
    Grid grid{};
    grid.resolution = stepsOf(constraints.resolution.value);
    grid.bar = std::max(1, static_cast<int>(std::lround(context.beatsPerBar * stepsPerBeat)));
    grid.start = static_cast<int>(std::ceil(context.fromBeats / stepBeats - epsilon));
    grid.start = (grid.start + grid.resolution - 1) / grid.resolution * grid.resolution;
    grid.end = toStep(context.toBeats);
    return grid;
}

// The next inter-onset interval: legal ones are on the resolution, inside
// the range and, for chords, inside the bar.
[[nodiscard]] std::optional<int> nextInterval(const RoleStyle& style,
                                              const ResolvedConstraints& constraints,
                                              const Grid& grid,
                                              int position,
                                              int limit,
                                              const std::vector<int>& history,
                                              Random& random)
{
    std::vector<int> candidates;
    for (int interval = grid.resolution; interval <= maxOnsetGap && position + interval <= limit;
         interval += grid.resolution)
        candidates.push_back(interval);
    if (candidates.empty())
        return std::nullopt;

    auto weights = smoothed(
        style.rhythm, rhythmContexts(position % grid.bar % 16, history), candidates, candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        const auto interval = static_cast<double>(candidates[i]);
        if (constraints.density.value == Density::sparse)
            weights[i] *= std::pow(interval, densityBend);
        else if (constraints.density.value == Density::dense)
            weights[i] *= std::pow(interval, -densityBend);
    }
    return candidates[random.pick(weights)];
}

[[nodiscard]] int nextDuration(const RoleStyle& style, int interval, Random& random)
{
    std::vector<int> lengths;
    for (int length = 1; length <= interval; ++length)
        lengths.push_back(length);
    const auto weights =
        smoothed(style.duration, durationContexts(interval), lengths, static_cast<std::size_t>(interval));
    return static_cast<int>(random.pick(weights)) + 1;
}

// --- one line: a melody, a bass, or a rhythm channel ---------------------------

[[nodiscard]] std::vector<GhostNote> generateLine(const Context& context,
                                                  const ResolvedConstraints& constraints,
                                                  const RoleStyle& style,
                                                  Random& random)
{
    const auto role = constraints.role.value;
    const auto key = constraints.key.value;
    const auto grid = gridOf(context, constraints);

    const auto [low, high] = registerRange(role, constraints.reg.value);
    const auto pitches = legalPitches(key, low, high);
    const auto chords = chordsOf(context, key);
    const auto firstBar = static_cast<int>(std::floor(context.fromBeats / context.beatsPerBar + epsilon));

    // What the row played before the range is where the line comes from.
    std::vector<Note> before;
    for (const auto& note : context.row)
    {
        if (note.startBeats < context.fromBeats - epsilon)
            before.push_back(note);
    }
    const auto line = lineOf(before, role);

    std::vector<int> onsets;
    std::vector<int> intervals;
    std::optional<int> previous;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (i > 0)
            keepLast(onsets,
                     std::clamp(toStep(line[i].startBeats) - toStep(line[i - 1].startBeats), 1, maxOnsetGap));

        const auto index = diatonicIndex(line[i].pitch, key);
        if (!index.has_value())
            continue;
        if (previous.has_value())
            keepLast(intervals, std::clamp(*index - *previous, -maxInterval, maxInterval));
        previous = index;
    }

    std::vector<GhostNote> out;
    for (auto position = grid.start; position < grid.end;)
    {
        const auto interval = nextInterval(style, constraints, grid, position, grid.end, onsets, random);
        if (!interval.has_value())
            break;

        GhostNote note{};
        note.startBeats = position * stepBeats;
        note.lengthBeats = nextDuration(style, *interval, random) * stepBeats;
        note.velocity = velocityAt(style, position % grid.bar, random);

        if (role == Role::rhythm)
        {
            note.pitch = context.channelPitch;
        }
        else if (!pitches.empty())
        {
            const auto strong = position % grid.bar % stepsPerBeat == 0;
            const auto bar =
                static_cast<int>(std::floor(note.startBeats / context.beatsPerBar + epsilon)) - firstBar;
            const auto chord = bar >= 0 && bar < static_cast<int>(chords.size())
                                   ? chords[static_cast<std::size_t>(bar)]
                                   : std::optional<Chord>{};
            // Every candidate's step from the previous note and its degree, then
            // the two tables asked once each.
            std::vector<int> steps;
            std::vector<int> degrees;
            for (const auto pitch : pitches)
            {
                const auto index = *diatonicIndex(pitch, key);
                steps.push_back(previous.has_value() ? index - *previous : 0);
                degrees.push_back(((index % 7) + 7) % 7);
            }

            auto weights =
                previous.has_value()
                    ? smoothed(style.interval, intervalContexts(intervals), steps, 2 * maxInterval + 1)
                    : std::vector<double>(pitches.size(), 1.0);
            const auto byDegree = smoothed(style.degree, degreeContexts(strong), degrees, 7);
            for (std::size_t i = 0; i < pitches.size(); ++i)
            {
                if (std::abs(steps[i]) > maxInterval)
                    weights[i] = 1e-4;
                weights[i] *= byDegree[i];
                if (chord.has_value() && isChordTone(pitches[i], *chord, key))
                    weights[i] *= strong ? chordToneOnBeat : chordToneOffBeat;
            }

            note.pitch = pitches[random.pick(weights)];
            const auto index = *diatonicIndex(note.pitch, key);
            if (previous.has_value())
                keepLast(intervals, std::clamp(index - *previous, -maxInterval, maxInterval));
            previous = index;
        }
        else
        {
            break;
        }

        out.push_back(note);
        keepLast(onsets, *interval);
        position += *interval;
    }

    return out;
}

// --- chords ----------------------------------------------------------------------

// Close voicings of a triad inside the window: root position and both
// inversions, at every octave that fits.
[[nodiscard]] std::vector<std::array<int, 3>> voicings(Chord chord, Key key, int low, int high)
{
    std::vector<std::array<int, 3>> out;
    for (int octave = -1; octave <= 10; ++octave)
    {
        const auto root = octave * 7 + chord.root;
        for (const auto& shape :
             {std::array<int, 3>{0, 2, 4}, std::array<int, 3>{2, 4, 7}, std::array<int, 3>{4, 7, 9}})
        {
            std::array<int, 3> pitches{};
            bool fits = true;
            for (std::size_t i = 0; i < 3; ++i)
            {
                pitches[i] = pitchOfIndex(root + shape[i], key);
                fits = fits && pitches[i] >= low && pitches[i] <= high;
            }
            if (fits)
                out.push_back(pitches);
        }
    }
    return out;
}

[[nodiscard]] std::vector<GhostNote> generateChords(const Context& context,
                                                    const ResolvedConstraints& constraints,
                                                    const RoleStyle& style,
                                                    Random& random)
{
    const auto key = constraints.key.value;
    const auto grid = gridOf(context, constraints);
    const auto [low, high] = registerRange(Role::chords, constraints.reg.value);
    const auto heard = chordsOf(context, key);
    const auto firstBar = static_cast<int>(std::floor(context.fromBeats / context.beatsPerBar + epsilon));

    std::vector<int> roots;
    std::vector<int> onsets;
    std::optional<std::array<int, 3>> voiced;
    int currentBar = -1;
    Chord chord{};

    std::vector<GhostNote> out;
    for (auto position = grid.start; position < grid.end;)
    {
        const auto bar = position / grid.bar;
        if (bar != currentBar)
        {
            currentBar = bar;
            const auto rank = bar - firstBar;
            const auto given = rank >= 0 && rank < static_cast<int>(heard.size())
                                   ? heard[static_cast<std::size_t>(rank)]
                                   : std::optional<Chord>{};
            if (given.has_value())
            {
                chord = *given;
            }
            else
            {
                std::vector<int> candidates;
                for (int root = 0; root < 7; ++root)
                {
                    if (!isDiminished(Chord{root}, key))
                        candidates.push_back(root);
                }
                const auto weights = smoothed(style.progression, progressionContexts(roots), candidates, 6);
                chord = Chord{candidates[random.pick(weights)]};
            }
            keepLast(roots, chord.root);

            // The voicing nearest the last one: voices move as little as they can.
            const auto options = voicings(chord, key, low, high);
            if (options.empty())
                break;

            auto best = options.front();
            auto bestDistance = 1 << 20;
            const auto centre = (low + high) / 2;
            for (const auto& option : options)
            {
                auto distance = 0;
                for (std::size_t i = 0; i < 3; ++i)
                    distance += std::abs(option[i] - (voiced.has_value() ? (*voiced)[i] : centre));
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = option;
                }
            }
            voiced = best;
        }

        const auto nextBar = (bar + 1) * grid.bar;
        const auto interval =
            nextInterval(style, constraints, grid, position, std::min(grid.end, nextBar), onsets, random);
        if (!interval.has_value())
        {
            position = nextBar;
            continue;
        }

        const auto length = nextDuration(style, *interval, random);
        const auto velocity = velocityAt(style, position % grid.bar, random);
        for (const auto pitch : *voiced)
            out.push_back(GhostNote{pitch, velocity, position * stepBeats, length * stepBeats});

        keepLast(onsets, *interval);
        position += *interval;
    }

    return out;
}

} // namespace

// --- Context -----------------------------------------------------------------------

Result<Context>
Context::of(const ProjectState& state, PatternId patternId, TrackId trackId, double fromBeats, double toBeats)
{
    const auto* pattern = state.findPattern(patternId);
    if (pattern == nullptr)
        return fail(ErrorCode::notFound, "no such pattern");

    const auto* owner = state.findTrack(trackId);
    if (owner == nullptr)
        return fail(ErrorCode::notFound, "no such track");

    Context context{};
    context.patternLength = pattern->lengthBeats;
    context.beatsPerBar = state.beatsPerBar();
    context.fromBeats = std::max(0.0, std::floor(fromBeats / stepBeats + epsilon) * stepBeats);
    context.toBeats = std::min(pattern->lengthBeats, std::ceil(toBeats / stepBeats - epsilon) * stepBeats);
    if (context.toBeats - context.fromBeats < stepBeats - epsilon)
        return fail(ErrorCode::invalidArgument, "the range is empty");

    context.trackName = owner->name;
    context.sampleChannel = owner->sample.has_value();
    context.channelPitch = owner->channelPitch;

    // What says something about the harmony: not a sample channel, and not a
    // row that plays one pitch only -- a kick drawn in the rack on 4OSC is a
    // drum, whatever its instrument, and its C would make the key C.
    const auto pitched = [&state](const Clip& clip)
    {
        const auto* track = state.findTrack(clip.trackId);
        if (track == nullptr || track->sample.has_value() || clip.notes.empty())
            return false;
        return std::any_of(clip.notes.begin(),
                           clip.notes.end(),
                           [&clip](const Note& note) { return note.pitch != clip.notes.front().pitch; });
    };

    for (const auto& clip : pattern->clips)
    {
        if (clip.trackId == trackId)
            context.row = clip.notes;
        else if (pitched(clip))
            context.harmony.insert(context.harmony.end(), clip.notes.begin(), clip.notes.end());
    }

    for (const auto& other : state.patterns())
    {
        for (const auto& clip : other.clips)
        {
            if (pitched(clip))
                context.project.insert(context.project.end(), clip.notes.begin(), clip.notes.end());
        }
    }

    return context;
}

std::uint64_t Context::hash() const
{
    Hasher hasher;
    hasher.add(patternLength);
    hasher.add(beatsPerBar);
    hasher.add(fromBeats);
    hasher.add(toBeats);
    hasher.add(row);
    hasher.add(harmony);
    // The rest of the project only matters when nothing near the range says
    // the key: a note written in another pattern must not regenerate a
    // proposal that never read it.
    if (row.empty() && harmony.empty())
        hasher.add(project);
    hasher.add(static_cast<std::int64_t>(sampleChannel ? 1 : 0));
    hasher.add(static_cast<std::int64_t>(channelPitch));
    for (const auto c : trackName)
        hasher.add(static_cast<std::int64_t>(static_cast<unsigned char>(c)));
    return hasher.value();
}

// --- resolution ----------------------------------------------------------------------

ResolvedConstraints resolve(const Constraints& constraints, const Context& context)
{
    ResolvedConstraints out{};

    if (constraints.role.has_value())
        out.role = {*constraints.role, Source::imposed};
    else
    {
        Source source{};
        const auto role = deducedRole(context, source);
        out.role = {role, source};
    }

    if (constraints.key.has_value())
    {
        out.key = {*constraints.key, Source::imposed};
    }
    else
    {
        auto near = weighted(context.harmony);
        if (!context.sampleChannel)
        {
            const auto own = weighted(context.row);
            near.insert(near.end(), own.begin(), own.end());
        }

        if (const auto key = detectKey(near); key.has_value())
            out.key = {*key, Source::deduced};
        else if (const auto far = detectKey(weighted(context.project)); far.has_value())
            out.key = {*far, Source::deduced};
        else
            out.key = {Key{9, Mode::minor}, Source::defaulted};
    }

    if (constraints.resolution.has_value())
    {
        out.resolution = {*constraints.resolution, Source::imposed};
    }
    else if (!context.row.empty())
    {
        // The finest grid the row already uses.
        auto finest = Resolution::quarter;
        for (const auto& note : context.row)
        {
            const auto steps = toStep(note.startBeats);
            if (steps % 2 != 0)
                finest = Resolution::sixteenth;
            else if (steps % 4 != 0 && finest == Resolution::quarter)
                finest = Resolution::eighth;
        }
        out.resolution = {finest, Source::deduced};
    }
    else
    {
        out.resolution = {Resolution::sixteenth, Source::defaulted};
    }

    out.density = constraints.density.has_value() ? Resolved<Density>{*constraints.density, Source::imposed}
                                                  : Resolved<Density>{Density::medium, Source::defaulted};

    if (constraints.reg.has_value())
    {
        out.reg = {*constraints.reg, Source::imposed};
    }
    else if (!context.row.empty() && out.role.value != Role::rhythm)
    {
        const auto median = medianPitch(context.row);
        auto best = Register::mid;
        auto bestDistance = 1 << 20;
        for (const auto reg : {Register::mid, Register::low, Register::high})
        {
            const auto [low, high] = registerRange(out.role.value, reg);
            const auto distance = std::abs(median - (low + high) / 2);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = reg;
            }
        }
        out.reg = {best, Source::deduced};
    }
    else
    {
        out.reg = {out.role.value == Role::bass ? Register::low : Register::mid, Source::defaulted};
    }

    return out;
}

std::vector<std::optional<Chord>> chordsOf(const Context& context, Key key)
{
    std::vector<std::optional<Chord>> out;
    const auto bar = context.beatsPerBar;
    for (auto start = std::floor(context.fromBeats / bar + epsilon) * bar; start < context.toBeats - epsilon;
         start += bar)
    {
        const auto end = start + bar;
        std::vector<WeightedPitch> heard;
        for (const auto& note : context.harmony)
        {
            const auto overlap =
                std::min(end, note.startBeats + note.lengthBeats) - std::max(start, note.startBeats);
            if (overlap > epsilon)
                heard.push_back({note.pitch, overlap});
        }
        out.push_back(detectChord(heard, key));
    }
    return out;
}

std::vector<GhostNote>
generate(const Context& context, const ResolvedConstraints& constraints, const StyleModel& model, int variant)
{
    Hasher seed;
    seed.add(static_cast<std::int64_t>(context.hash()));
    seed.add(static_cast<std::int64_t>(constraints.key.value.tonic));
    seed.add(static_cast<std::int64_t>(constraints.key.value.mode));
    seed.add(static_cast<std::int64_t>(constraints.resolution.value));
    seed.add(static_cast<std::int64_t>(constraints.density.value));
    seed.add(static_cast<std::int64_t>(constraints.reg.value));
    seed.add(static_cast<std::int64_t>(constraints.role.value));
    seed.add(static_cast<std::int64_t>(variant));

    Random random{seed.value()};
    const auto& style = model.role(constraints.role.value);

    if (constraints.role.value == Role::chords)
        return generateChords(context, constraints, style, random);
    return generateLine(context, constraints, style, random);
}

// --- variants ------------------------------------------------------------------------

Variants::Variants(Context context, ResolvedConstraints constraints, const StyleModel& model)
    : context_{std::move(context)}
    , constraints_{constraints}
    , model_{model}
{
}

const std::vector<GhostNote>& Variants::at(int rank)
{
    // Four draws per variant at most: a range of one sixteenth has one answer,
    // and asking for sixteen must not loop forever.
    constexpr int drawsPerVariant = 4;
    const auto wanted = std::clamp(rank, 0, maxVariants - 1);

    while (static_cast<int>(drawn_.size()) <= wanted && nextSeed_ < maxVariants * drawsPerVariant)
    {
        auto notes = generate(context_, constraints_, model_, nextSeed_++);
        if (std::find(drawn_.begin(), drawn_.end(), notes) == drawn_.end())
            drawn_.push_back(std::move(notes));
    }

    if (drawn_.empty())
        drawn_.emplace_back();
    return drawn_[static_cast<std::size_t>(std::min(wanted, static_cast<int>(drawn_.size()) - 1))];
}

} // namespace daw::domain::generation
