#include "daw/domain/live/Take.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/PatternCommands.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace daw::domain::live
{
namespace
{

constexpr int sustainController = 64;

} // namespace

TakeBuilder::TakeBuilder(TakeTiming timing)
    : timing_(std::move(timing))
{
    if (!timing_.beatsAt)
        timing_.beatsAt = [](double seconds) { return seconds * 2.0; }; // 120 BPM
}

double TakeBuilder::heardSeconds(double seconds, const Position& position) const
{
    // The song moves with the clock while it plays. What is heard at an
    // instant is what was rendered a latency earlier.
    const auto rendered = position.editSeconds + (position.playing ? seconds - position.clock : 0.0);
    return rendered - timing_.latencySeconds;
}

double TakeBuilder::wrapped(double beats) const
{
    if (timing_.loopLengthBeats <= 0.0)
        return beats;
    const auto into = std::fmod(beats - timing_.loopStartBeats, timing_.loopLengthBeats);
    return timing_.loopStartBeats + (into < 0.0 ? into + timing_.loopLengthBeats : into);
}

double TakeBuilder::beatsHeard(double seconds, const Position& position) const
{
    return wrapped(timing_.beatsAt(heardSeconds(seconds, position)));
}

double TakeBuilder::lengthOf(const Held& held, double end) const
{
    // By the time held, from where it started: a note held over the end of
    // the loop is as long as it was held, not cut by the wrap.
    const auto start = timing_.beatsAt(held.heard);
    const auto finish = timing_.beatsAt(held.heard + std::max(0.0, end - held.pressed));
    auto length = std::max(shortestBeats, finish - start);

    // In a loop, a note ends at the end of the pattern: the next pass is
    // another pass, not the same note.
    if (timing_.loopLengthBeats > 0.0)
        length = std::min(length, timing_.loopStartBeats + timing_.loopLengthBeats - held.note.startBeats);
    return std::max(shortestBeats, length);
}

void TakeBuilder::close(std::size_t index, double end)
{
    auto held = held_[index];
    held.note.lengthBeats = lengthOf(held, end);
    held.note.open = false;
    closed_.emplace_back(held.pressed, held.note);
    held_.erase(held_.begin() + static_cast<std::ptrdiff_t>(index));
}

void TakeBuilder::add(const Event& event, const TrackId& track, const Position& position)
{
    const auto status = event.bytes[0] & 0xF0;
    const auto pitch = static_cast<int>(event.bytes[1] & 0x7F);
    const auto value = static_cast<int>(event.bytes[2] & 0x7F);

    if (status == 0xB0 && pitch == sustainController)
    {
        const bool down = value >= 64;
        pedal_[track] = down;
        if (down)
            return;
        // The pedal up ends what it was holding.
        for (std::size_t index = held_.size(); index-- > 0;)
        {
            if (held_[index].note.track == track && held_[index].released)
                close(index, event.seconds);
        }
        return;
    }

    const bool on = status == 0x90 && value > 0;
    const bool off = status == 0x80 || (status == 0x90 && value == 0);
    if (on)
    {
        Held held;
        held.note.track = track;
        held.note.pitch = pitch;
        held.note.velocity = std::clamp(value, Note::lowestVelocity, Note::highestVelocity);
        held.heard = heardSeconds(event.seconds, position);
        held.note.startBeats = wrapped(timing_.beatsAt(held.heard));
        if (timing_.loopLengthBeats > 0.0 &&
            held.note.startBeats > timing_.loopStartBeats + timing_.loopLengthBeats - wrapBeats)
            held.note.startBeats = timing_.loopStartBeats;
        held.note.open = true;
        held.pressed = event.seconds;
        held_.push_back(held);
        return;
    }
    if (!off)
        return;

    for (std::size_t index = 0; index < held_.size(); ++index)
    {
        auto& held = held_[index];
        if (held.note.track != track || held.note.pitch != pitch || held.released)
            continue;
        const auto pedalDown = pedal_.contains(track) && pedal_.at(track);
        if (pedalDown)
        {
            held.released = true;
            return;
        }
        close(index, event.seconds);
        return;
    }
}

void TakeBuilder::stop(double seconds, const Position& position)
{
    static_cast<void>(position);
    while (!held_.empty())
        close(0, seconds);
    pedal_.clear();
}

std::vector<TakeNote> TakeBuilder::notes(double seconds, const Position& position) const
{
    static_cast<void>(position);
    auto all = closed_;
    for (const auto& held : held_)
    {
        auto note = held.note;
        note.lengthBeats = lengthOf(held, seconds);
        all.emplace_back(held.pressed, note);
    }
    // In the order they were played.
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<TakeNote> out;
    out.reserve(all.size());
    for (const auto& entry : all)
        out.push_back(entry.second);
    return out;
}

std::vector<std::unique_ptr<Command>> takeCommands(const ProjectState& state,
                                                   const TakeDestination& destination,
                                                   const std::vector<TakeNote>& notes,
                                                   const TakeIds& ids)
{
    std::vector<std::unique_ptr<Command>> commands;
    if (notes.empty() || !ids.note || !ids.row)
        return commands;

    const bool song = destination.mode == PlayMode::song;
    const Pattern* existing = song ? nullptr : state.findPattern(destination.pattern);
    if (!song && existing == nullptr)
        return commands; // no pattern to write into: nothing is written, the screen says so

    if (song)
    {
        commands.push_back(std::make_unique<CreatePattern>(
            destination.pattern, std::string{}, std::max(destination.lengthBeats, shortestBeats)));
        commands.push_back(
            std::make_unique<PlacePattern>(destination.placement, destination.pattern, destination.atBeats));
    }

    // The rows: the pattern's own, opened for a track that has none.
    std::map<TrackId, ClipId> rows;
    std::map<ClipId, std::vector<std::pair<int, double>>> taken; // pitch, start: what each row holds
    for (const auto& note : notes)
    {
        if (rows.contains(note.track))
            continue;
        const Clip* clip = existing != nullptr ? existing->findClipForTrack(note.track) : nullptr;
        if (clip != nullptr)
        {
            rows[note.track] = clip->id;
            for (const auto& held : clip->notes)
                taken[clip->id].emplace_back(held.pitch, held.startBeats);
        }
        else
        {
            const auto row = ids.row();
            rows[note.track] = row;
            commands.push_back(std::make_unique<AddPatternTrack>(destination.pattern, row, note.track));
        }
    }

    for (const auto& played : notes)
    {
        const auto row = rows.at(played.track);
        const auto start = song ? played.startBeats - destination.atBeats : played.startBeats;
        if (start < 0.0)
            continue;

        // A second pass over the same note adds nothing.
        auto& already = taken[row];
        const auto same = std::any_of(already.begin(),
                                      already.end(),
                                      [&played, start](const std::pair<int, double>& other) {
                                          return other.first == played.pitch &&
                                                 std::abs(other.second - start) < sameStartBeats;
                                      });
        if (same)
            continue;
        already.emplace_back(played.pitch, start);

        Note note;
        note.id = ids.note();
        note.pitch = std::clamp(played.pitch, Note::lowestPitch, Note::highestPitch);
        note.velocity = std::clamp(played.velocity, Note::lowestVelocity, Note::highestVelocity);
        note.startBeats = start;
        note.lengthBeats = std::max(shortestBeats, played.lengthBeats);
        commands.push_back(std::make_unique<AddNote>(row, note));
    }

    // Only rows and no note (every note a double): nothing to write.
    const auto anyNote = std::any_of(commands.begin(),
                                     commands.end(),
                                     [](const std::unique_ptr<Command>& command)
                                     { return command->type() == AddNote::commandType; });
    if (!anyNote)
        commands.clear();
    return commands;
}

} // namespace daw::domain::live
