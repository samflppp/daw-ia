#include "daw/domain/generation/Generator.h"

#include "daw/domain/generation/Form.h"

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

[[nodiscard]] Grid gridOf(const Context& context, Resolution resolution)
{
    Grid grid{};
    grid.resolution = stepsOf(resolution);
    grid.bar = std::max(1, static_cast<int>(std::lround(context.beatsPerBar * stepsPerBeat)));
    grid.start = static_cast<int>(std::ceil(context.fromBeats / stepBeats - epsilon));
    grid.start = (grid.start + grid.resolution - 1) / grid.resolution * grid.resolution;
    grid.end = toStep(context.toBeats);
    return grid;
}

[[nodiscard]] Grid gridOf(const Context& context, const ResolvedConstraints& constraints)
{
    return gridOf(context, constraints.resolution.value);
}

// How the range is cut into units: their length, and how many there are. A
// range that does not fall on a unit counts its last piece as a unit, which
// gets the beginning of the one it repeats.
struct Layout
{
    int unitSteps{16};
    int units{1};
};

[[nodiscard]] Layout layoutOf(const Grid& grid, Role role)
{
    Layout out{};
    const auto range = std::max(0, grid.end - grid.start);
    const auto bars = (range + grid.bar - 1) / grid.bar;
    out.unitSteps = unitBars(role, bars) * grid.bar;
    out.units = std::max(1, (range + out.unitSteps - 1) / out.unitSteps);
    return out;
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

// --- form ------------------------------------------------------------------------

// Lays one unit out over the range, letter by letter (Form.h). Every change it
// makes stays inside what the rules allow: pitches come from the legal list,
// onsets stay on the resolution and inside the range.
class Shaper
{
public:
    Shaper(const Context& context,
           const ResolvedConstraints& constraints,
           const RoleStyle& style,
           Random& random,
           const Layout& layout)
        : context_{context}
        , constraints_{constraints}
        , style_{style}
        , random_{random}
        , grid_{gridOf(context, constraints)}
        , unitSteps_{layout.unitSteps}
        , letters_{schema(constraints.form.value, layout.units)}
        , firstBar_{static_cast<int>(std::floor(context.fromBeats / context.beatsPerBar + epsilon))}
        , chords_{chordsOf(context, constraints.key.value)}
    {
        const auto [low, high] = registerRange(constraints.role.value, constraints.reg.value);
        pitches_ = legalPitches(constraints.key.value, low, high);
        for (const auto& note : context.row)
        {
            if (note.startBeats < context.fromBeats - epsilon)
                before_.push_back(note);
        }
    }

    [[nodiscard]] std::vector<GhostNote> run()
    {
        if (letters_.empty())
            return generateLine(context_, constraints_, style_, random_);

        auto unitContext = context_;
        unitContext.toBeats = std::min(context_.toBeats, (grid_.start + unitSteps_) * stepBeats);
        const auto unit = generateLine(unitContext, constraints_, style_, random_);
        if (unit.empty())
            return unit;

        std::vector<GhostNote> out;
        for (std::size_t i = 0; i < letters_.size(); ++i)
        {
            const auto index = static_cast<int>(i);
            const auto unitStart = grid_.start + index * unitSteps_;
            if (unitStart >= grid_.end)
                break;
            const auto unitEnd = std::min(grid_.end, unitStart + unitSteps_);

            auto piece = within(shifted(unit, index * unitSteps_), unitStart, unitEnd);
            switch (letters_[i])
            {
            case Letter::a:
                adapt(piece, index);
                break;
            case Letter::aReturn:
                adapt(piece, index);
                static_cast<void>(end(piece, true));
                break;
            case Letter::aVaried:
                adapt(piece, index);
                vary(piece);
                break;
            case Letter::b:
                piece = contrast(piece, index, unitStart, unitEnd, out);
                break;
            }
            out.insert(out.end(), piece.begin(), piece.end());
        }
        return out;
    }

private:
    [[nodiscard]] static std::vector<GhostNote> shifted(std::vector<GhostNote> notes, int steps)
    {
        for (auto& note : notes)
            note.startBeats += steps * stepBeats;
        return notes;
    }

    // The notes that start between two steps, cut at the second.
    [[nodiscard]] static std::vector<GhostNote> within(const std::vector<GhostNote>& notes, int from, int to)
    {
        std::vector<GhostNote> out;
        for (auto note : notes)
        {
            if (note.startBeats < from * stepBeats - epsilon || note.startBeats > to * stepBeats - epsilon)
                continue;
            note.lengthBeats = std::min(note.lengthBeats, to * stepBeats - note.startBeats);
            out.push_back(note);
        }
        return out;
    }

    [[nodiscard]] static std::vector<int> onsets(const std::vector<GhostNote>& notes, int from)
    {
        std::vector<int> out;
        for (const auto& note : notes)
            out.push_back(toStep(note.startBeats) - from);
        return out;
    }

    [[nodiscard]] std::optional<Chord> chordAt(int step) const
    {
        const auto bar =
            static_cast<int>(std::floor(step * stepBeats / context_.beatsPerBar + epsilon)) - firstBar_;
        if (bar < 0 || bar >= static_cast<int>(chords_.size()))
            return std::nullopt;
        return chords_[static_cast<std::size_t>(bar)];
    }

    [[nodiscard]] int degreeOf(int pitch) const
    {
        const auto index = diatonicIndex(pitch, constraints_.key.value).value_or(0);
        return ((index % 7) + 7) % 7;
    }

    // The legal pitch nearest a target that passes a test; the lower on a tie.
    template <typename Test>
    [[nodiscard]] std::optional<int> nearest(int target, Test test) const
    {
        std::optional<int> best;
        for (const auto pitch : pitches_)
        {
            if (test(pitch) && (!best.has_value() || std::abs(pitch - target) < std::abs(*best - target)))
                best = pitch;
        }
        return best;
    }

    // A repeated unit over another chord than the one under the first unit.
    void adapt(std::vector<GhostNote>& piece, int unit) const
    {
        const auto role = constraints_.role.value;
        if (unit == 0 || role == Role::rhythm || role == Role::chords)
            return;

        const auto key = constraints_.key.value;
        const auto [low, high] = registerRange(role, constraints_.reg.value);
        for (auto& note : piece)
        {
            const auto step = toStep(note.startBeats);
            const auto now = chordAt(step);
            const auto then = chordAt(step - unit * unitSteps_);
            if (!now.has_value() || !then.has_value() || *now == *then)
                continue;

            if (role == Role::bass)
            {
                // Follow the root: the same contour, moved by the step between
                // the two roots, the short way round.
                auto delta = (((now->root - then->root) % 7) + 7) % 7;
                if (delta > 3)
                    delta -= 7;
                const auto index = diatonicIndex(note.pitch, key);
                if (!index.has_value())
                    continue;
                for (const auto octave : {0, -7, 7})
                {
                    const auto pitch = pitchOfIndex(*index + delta + octave, key);
                    if (pitch >= low && pitch <= high)
                    {
                        note.pitch = pitch;
                        break;
                    }
                }
            }
            else if (step % grid_.bar % stepsPerBeat == 0 && !isChordTone(note.pitch, *now, key))
            {
                const auto pitch =
                    nearest(note.pitch, [&](int candidate) { return isChordTone(candidate, *now, key); });
                if (pitch.has_value())
                    note.pitch = *pitch;
            }
        }
    }

    // The last note closed on the tonic, or opened: on the fifth for a bass,
    // whose second rubs against the root it leaves, on the second or the fifth
    // for a melody. Says whether it changed.
    bool end(std::vector<GhostNote>& piece, bool closed) const
    {
        if (piece.empty() || constraints_.role.value == Role::rhythm)
            return false;

        const auto bass = constraints_.role.value == Role::bass;
        auto& last = piece.back();
        const auto pitch = nearest(last.pitch,
                                   [&](int candidate)
                                   {
                                       const auto degree = degreeOf(candidate);
                                       if (closed)
                                           return degree == 0;
                                       return degree == 4 || (!bass && degree == 1);
                                   });
        if (!pitch.has_value() || *pitch == last.pitch)
            return false;
        last.pitch = *pitch;
        return true;
    }

    // One note moved by one step of the resolution, the downbeat kept when
    // another note can move. Says whether one could.
    bool displace(std::vector<GhostNote>& piece) const
    {
        const auto step = grid_.resolution * stepBeats;
        struct Move
        {
            std::size_t index;
            bool later;
        };
        std::vector<Move> moves;
        for (std::size_t i = 1; i < piece.size(); ++i)
        {
            if (piece[i].lengthBeats > step + epsilon)
                moves.push_back({i, true});
            if (piece[i].startBeats - step > piece[i - 1].startBeats + epsilon)
                moves.push_back({i, false});
        }
        if (moves.empty() && !piece.empty() && piece.front().lengthBeats > step + epsilon)
            moves.push_back({0, true});
        if (moves.empty())
            return false;

        const auto move = moves[random_.next() % moves.size()];
        auto& note = piece[move.index];
        if (move.later)
        {
            note.startBeats += step;
            note.lengthBeats -= step;
        }
        else
        {
            note.startBeats -= step;
            note.lengthBeats += step;
            auto& previous = piece[move.index - 1];
            previous.lengthBeats = std::min(previous.lengthBeats, note.startBeats - previous.startBeats);
        }
        return true;
    }

    void accent(std::vector<GhostNote>& piece) const
    {
        if (piece.empty())
            return;
        auto& note = piece[random_.next() % piece.size()];
        const auto delta = random_.next() % 2 == 0 ? 15 : -15;
        auto velocity = std::clamp(note.velocity + delta, Note::lowestVelocity, Note::highestVelocity);
        if (velocity == note.velocity)
            velocity = std::clamp(note.velocity - delta, Note::lowestVelocity, Note::highestVelocity);
        note.velocity = velocity;
    }

    // A light change, never a redraw.
    void vary(std::vector<GhostNote>& piece) const
    {
        const auto pitched = constraints_.role.value != Role::rhythm;
        const auto choice = random_.next() % (pitched ? 3 : 2);
        if (choice == 0 && displace(piece))
            return;
        if (choice == 2 && end(piece, random_.next() % 2 == 0))
            return;
        accent(piece);
    }

    // The first half kept, the second drawn again after it with another rhythm
    // than the unit had there, and an open end.
    [[nodiscard]] std::vector<GhostNote> contrast(std::vector<GhostNote> piece,
                                                  int unit,
                                                  int unitStart,
                                                  int unitEnd,
                                                  const std::vector<GhostNote>& written)
    {
        const auto half = unitStart + unitSteps_ / 2;
        adapt(piece, unit);
        if (half >= unitEnd)
            return piece;

        auto first = within(piece, unitStart, half);
        const auto reference = onsets(within(piece, half, unitEnd), half);

        // What the second half follows: the row before the range, then what
        // the form has written so far.
        auto sub = context_;
        sub.row = before_;
        const std::vector<GhostNote>& kept = first;
        for (const auto* part : {&written, &kept})
        {
            for (const auto& ghost : *part)
            {
                Note note{};
                note.pitch = ghost.pitch;
                note.velocity = ghost.velocity;
                note.startBeats = ghost.startBeats;
                note.lengthBeats = ghost.lengthBeats;
                sub.row.push_back(note);
            }
        }
        sub.fromBeats = half * stepBeats;
        sub.toBeats = unitEnd * stepBeats;

        // Four draws: a rhythm table that knows one answer gives the same
        // rhythm every time, and a moved note then makes the difference.
        std::vector<GhostNote> second;
        auto differs = false;
        for (int draw = 0; draw < 4 && !differs; ++draw)
        {
            second = generateLine(sub, constraints_, style_, random_);
            differs = onsets(second, half) != reference;
        }
        if (!differs)
            static_cast<void>(displace(second));

        first.insert(first.end(), second.begin(), second.end());
        static_cast<void>(end(first, false));
        return first;
    }

    const Context& context_;
    const ResolvedConstraints& constraints_;
    const RoleStyle& style_;
    Random& random_;
    Grid grid_;
    int unitSteps_;
    std::vector<Letter> letters_;
    int firstBar_;
    std::vector<std::optional<Chord>> chords_;
    std::vector<int> pitches_;
    std::vector<Note> before_;
};

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

// With a form, the comping rhythm of the first bar is the unit: every A bar
// plays it again, a varied one changes one velocity, a B bar draws its own.
// The progression stays drawn bar by bar, and past four bars it comes back.
[[nodiscard]] std::vector<GhostNote> generateChords(const Context& context,
                                                    const ResolvedConstraints& constraints,
                                                    const RoleStyle& style,
                                                    Random& random,
                                                    const Layout& layout)
{
    const auto key = constraints.key.value;
    const auto grid = gridOf(context, constraints);
    const auto letters = schema(constraints.form.value, layout.units);
    const auto startBar = grid.start / grid.bar;

    struct Hit
    {
        int offset;
        int length;
        int velocity;
    };
    std::vector<Hit> comping;
    std::vector<Chord> cycle;
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
            const auto unit = bar - startBar;
            if (given.has_value())
            {
                chord = *given;
            }
            else if (!letters.empty() && unit >= 4 && !cycle.empty())
            {
                chord = cycle[static_cast<std::size_t>(unit % 4) % cycle.size()];
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
            if (unit < 4)
                cycle.push_back(chord);

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
        const auto unit = bar - startBar;
        const auto letter = !letters.empty() && unit < static_cast<int>(letters.size())
                                ? letters[static_cast<std::size_t>(unit)]
                                : Letter::b;
        if (unit > 0 && letter != Letter::b && !comping.empty())
        {
            auto hits = comping;
            if (letter == Letter::aVaried)
            {
                auto& hit = hits[random.next() % hits.size()];
                hit.velocity = std::clamp(hit.velocity + (hit.velocity > 100 ? -15 : 15),
                                          Note::lowestVelocity,
                                          Note::highestVelocity);
            }
            for (const auto& hit : hits)
            {
                const auto at = bar * grid.bar + hit.offset;
                if (at < grid.start || at >= std::min(grid.end, nextBar))
                    continue;
                const auto length = std::min(hit.length, std::min(grid.end, nextBar) - at);
                for (const auto pitch : *voiced)
                    out.push_back(GhostNote{pitch, hit.velocity, at * stepBeats, length * stepBeats});
            }
            position = nextBar;
            continue;
        }

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
        if (!letters.empty() && unit == 0)
            comping.push_back(Hit{position - bar * grid.bar, length, velocity});

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

    if (constraints.form.has_value())
    {
        out.form = {*constraints.form, Source::imposed};
    }
    else
    {
        const auto layout = layoutOf(gridOf(context, out.resolution.value), out.role.value);
        out.form = {defaultForm(out.role.value, layout.units), Source::deduced};
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
    seed.add(static_cast<std::int64_t>(constraints.form.value));
    seed.add(static_cast<std::int64_t>(variant));

    Random random{seed.value()};
    const auto& style = model.role(constraints.role.value);
    const auto layout = layoutOf(gridOf(context, constraints), constraints.role.value);

    if (constraints.role.value == Role::chords)
        return generateChords(context, constraints, style, random, layout);
    return Shaper{context, constraints, style, random, layout}.run();
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
