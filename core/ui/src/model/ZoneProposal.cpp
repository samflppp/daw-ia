#include "daw/ui/model/ZoneProposal.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteCommands.h"
#include "daw/domain/commands/PatternCommands.h"
#include "daw/domain/generation/Phrase.h"
#include "daw/domain/serialization/Json.h"
#include "daw/ui/model/GhostProposal.h"

#include <algorithm>
#include <cmath>

namespace daw::ui
{
namespace
{

using domain::generation::Role;

constexpr double epsilon = 1e-9;

// The order the parts are drawn in: what the others are built on first.
[[nodiscard]] int drawingOrder(Role role) noexcept
{
    switch (role)
    {
    case Role::chords:
        return 0;
    case Role::bass:
        return 1;
    case Role::melody:
        return 2;
    case Role::rhythm:
        return 3;
    }
    return 4;
}

[[nodiscard]] std::optional<Role> roleOfTrack(const domain::ProjectState& state,
                                              domain::TrackId track,
                                              const domain::tidy::PresetNames& presets)
{
    return domain::tidy::generationRole(domain::tidy::classifyTrack(state, track, presets).family);
}

[[nodiscard]] bool startsIn(const domain::Note& note, double from, double to)
{
    return note.startBeats >= from - epsilon && note.startBeats < to - epsilon;
}

} // namespace

domain::Result<ZoneProposal> ZoneProposal::open(const domain::ProjectState& state,
                                                Zone zone,
                                                domain::generation::Interpretation interpretation,
                                                std::shared_ptr<const domain::generation::StyleModel> model,
                                                const domain::tidy::PresetNames& presets)
{
    if (model == nullptr)
        return domain::fail(domain::ErrorCode::invalidArgument, "no style model");
    if (zone.toBeats - zone.fromBeats < domain::generation::stepBeats - epsilon)
        return domain::fail(domain::ErrorCode::invalidArgument, "the zone is empty");

    ZoneProposal proposal;
    proposal.zone_ = zone;
    proposal.interpretation_ = std::move(interpretation);
    proposal.model_ = std::move(model);

    std::vector<domain::TrackId> used;
    for (const auto lane : zone.lanes)
    {
        const auto* line = state.findLane(lane);
        if (line == nullptr)
            continue;

        Part part{};
        part.lane = lane;
        part.guess = domain::tidy::classifyLane(state, lane, presets);
        auto role = domain::tidy::generationRole(part.guess.family);

        // One line alone, with no reading of its own: the prompt's role,
        // as in the piano roll.
        if (!role.has_value() && zone.lanes.size() == 1)
            role = proposal.interpretation_.constraints.role;
        if (!role.has_value())
        {
            proposal.skipped_.push_back(
                {lane,
                 "Je ne sais pas ce que joue cette ligne : nomme-la (Basse, Accords, Mélodie, Drums…)."});
            continue;
        }
        part.role = *role;

        // The track: one of the line's own that plays this role, else the
        // line's main one, else any track of the project that plays it.
        const auto own = domain::tidy::tracksOfLane(state, lane);
        std::optional<domain::TrackId> track;
        for (const auto candidate : own)
        {
            if (roleOfTrack(state, candidate, presets) == role)
            {
                track = candidate;
                break;
            }
        }
        if (!track.has_value() && !own.empty())
            track = own.front();
        if (!track.has_value())
        {
            for (const auto& candidate : state.tracks())
            {
                if (std::find(used.begin(), used.end(), candidate.id) == used.end() &&
                    roleOfTrack(state, candidate.id, presets) == role)
                {
                    track = candidate.id;
                    break;
                }
            }
        }
        if (!track.has_value())
        {
            proposal.skipped_.push_back({lane,
                                         "Aucune piste ne joue " +
                                             std::string{domain::generation::describe(*role)} +
                                             " : ajoute-en une, ou range un de ses blocs sur cette ligne."});
            continue;
        }
        part.track = *track;

        // Where: the block of the line the zone starts in, or a new one.
        const domain::Placement* under = nullptr;
        for (const auto& placement : state.arrangement())
        {
            if (placement.laneId != lane)
                continue;
            const auto* pattern = state.findPattern(placement.patternId);
            if (pattern != nullptr && placement.startBeats <= zone.fromBeats + epsilon &&
                zone.fromBeats < placement.startBeats + pattern->lengthBeats - epsilon)
                under = &placement;
        }
        if (under != nullptr)
        {
            const auto* pattern = state.findPattern(under->patternId);
            part.pattern = pattern->id;
            part.songBeats = under->startBeats;
            part.fromBeats = zone.fromBeats - under->startBeats;
            part.toBeats =
                std::min(zone.toBeats, under->startBeats + pattern->lengthBeats) - under->startBeats;
        }
        else
        {
            part.pattern = domain::PatternId::generate();
            part.isNew = true;
            part.name = domain::tidy::suggestedName(part.guess.family);
            part.songBeats = zone.fromBeats;
            part.lengthBeats = zone.toBeats - zone.fromBeats;
            part.fromBeats = 0.0;
            part.toBeats = part.lengthBeats;
        }

        const auto twice = std::any_of(proposal.parts_.begin(),
                                       proposal.parts_.end(),
                                       [&part](const Part& other) {
                                           return other.pattern == part.pattern && other.track == part.track;
                                       });
        if (twice)
        {
            proposal.skipped_.push_back({lane, "Cette ligne rejoue une piste déjà proposée plus haut."});
            continue;
        }

        used.push_back(part.track);
        proposal.parts_.push_back(std::move(part));
    }

    if (proposal.parts_.empty())
        return domain::fail(domain::ErrorCode::invalidArgument,
                            proposal.skipped_.empty() ? std::string{"no line in the zone"}
                                                      : proposal.skipped_.front().why);

    proposal.origin_ = std::make_shared<const domain::ProjectState>(state);
    proposal.signature_ = proposal.signature(state);
    proposal.draw(*proposal.origin_);
    return proposal;
}

void ZoneProposal::draw(const domain::ProjectState& state)
{
    // A copy the proposal writes into: the new patterns laid where they would
    // be, and each part drawn so far written where the next one hears it.
    auto shadow = state;
    for (const auto& part : parts_)
    {
        if (!part.isNew || shadow.findPattern(part.pattern) != nullptr)
            continue;
        domain::Pattern pattern{};
        pattern.id = part.pattern;
        pattern.name = part.name;
        pattern.lengthBeats = part.lengthBeats;
        static_cast<void>(shadow.addPattern(std::move(pattern)));

        domain::Placement laying{};
        laying.id = domain::PlacementId::generate();
        laying.patternId = part.pattern;
        laying.startBeats = part.songBeats;
        laying.laneId = part.lane;
        static_cast<void>(shadow.insertPlacement(laying, shadow.arrangement().size()));
    }

    std::vector<std::size_t> order(parts_.size());
    for (std::size_t index = 0; index < order.size(); ++index)
        order[index] = index;
    std::stable_sort(order.begin(),
                     order.end(),
                     [this](std::size_t lhs, std::size_t rhs)
                     { return drawingOrder(parts_[lhs].role) < drawingOrder(parts_[rhs].role); });

    drawn_ = 1;
    std::vector<std::size_t> done;
    for (const auto index : order)
    {
        auto& part = parts_[index];

        // What was drawn before, heard in this part's pattern over its range:
        // the generator reads the other rows of a pattern as its harmony.
        for (const auto earlierIndex : done)
        {
            const auto& earlier = parts_[earlierIndex];
            if (earlier.pattern == part.pattern && earlier.track == part.track)
                continue;

            const auto* pattern = shadow.findPattern(part.pattern);
            if (pattern == nullptr)
                continue;

            domain::ClipId row{};
            if (const auto* existing = pattern->findClipForTrack(earlier.track); existing != nullptr)
            {
                row = existing->id;
                std::vector<domain::NoteId> gone;
                for (const auto& note : existing->notes)
                {
                    if (startsIn(note, part.fromBeats, part.toBeats))
                        gone.push_back(note.id);
                }
                for (const auto id : gone)
                    static_cast<void>(shadow.removeNote(row, id));
            }
            else
            {
                domain::Clip clip{};
                clip.id = domain::ClipId::generate();
                clip.trackId = earlier.track;
                row = clip.id;
                if (!shadow.addClip(part.pattern, std::move(clip)).ok())
                    continue;
            }

            for (const auto& ghost : earlier.notes)
            {
                domain::Note note{};
                note.id = domain::NoteId::generate();
                note.pitch = ghost.pitch;
                note.velocity = ghost.velocity;
                note.lengthBeats = ghost.lengthBeats;
                note.startBeats = earlier.songBeats + ghost.startBeats - part.songBeats;
                if (startsIn(note, part.fromBeats, part.toBeats))
                    static_cast<void>(shadow.addNote(row, note));
            }
        }

        auto interpretation = interpretation_;
        interpretation.constraints.role = part.role;
        auto opened = GhostProposal::open(shadow,
                                          part.pattern,
                                          part.track,
                                          part.fromBeats,
                                          part.toBeats,
                                          std::move(interpretation),
                                          model_);
        if (!opened)
        {
            part.notes.clear();
            continue;
        }

        auto proposal = std::move(opened).value();
        static_cast<void>(proposal.shift(rank_));
        part.notes = proposal.notes();
        part.constraints = proposal.constraints();
        drawn_ = std::max(drawn_, proposal.drawn());
        done.push_back(index);
    }
}

int ZoneProposal::shift(int delta)
{
    const auto wanted = std::max(0, rank_ + delta);
    if (wanted == rank_ || origin_ == nullptr)
        return rank_;
    rank_ = wanted;
    draw(*origin_);
    return rank_;
}

std::string ZoneProposal::signature(const domain::ProjectState& state) const
{
    std::string out;
    for (const auto lane : zone_.lanes)
    {
        const auto* line = state.findLane(lane);
        out += line != nullptr ? domain::json::write(line->toValue()) : "-";
        for (const auto& placement : state.arrangement())
        {
            if (placement.laneId == lane)
                out += domain::json::write(placement.toValue());
        }
    }
    for (const auto& part : parts_)
    {
        out += state.findTrack(part.track) != nullptr ? "t" : "-";
        if (const auto* pattern = state.findPattern(part.pattern); pattern != nullptr && !part.isNew)
            out += domain::json::write(pattern->toValue());
    }
    return out;
}

bool ZoneProposal::stale(const domain::ProjectState& state) const
{
    return signature(state) != signature_;
}

ZoneProposal::Acceptance ZoneProposal::accept(const domain::ProjectState& state) const
{
    Acceptance out;
    std::string roles;

    for (const auto& part : parts_)
    {
        if (part.notes.empty())
            continue;

        domain::ClipId row{};
        if (part.isNew)
        {
            // The identifiers are drawn now, except the pattern's: it was
            // heard under this one.
            out.commands.push_back(
                std::make_unique<domain::CreatePattern>(part.pattern, part.name, part.lengthBeats, false));
            row = domain::ClipId::generate();
            out.commands.push_back(std::make_unique<domain::AddPatternTrack>(part.pattern, row, part.track));
            out.commands.push_back(std::make_unique<domain::PlacePattern>(
                domain::PlacementId::generate(), part.pattern, part.songBeats, part.lane));
        }
        else
        {
            const auto* pattern = state.findPattern(part.pattern);
            if (pattern == nullptr)
                continue;
            if (const auto* existing = pattern->findClipForTrack(part.track); existing != nullptr)
            {
                row = existing->id;
                for (const auto& note : existing->notes)
                {
                    if (startsIn(note, part.fromBeats, part.toBeats))
                        out.commands.push_back(std::make_unique<domain::RemoveNote>(row, note.id));
                }
            }
            else
            {
                row = domain::ClipId::generate();
                out.commands.push_back(
                    std::make_unique<domain::AddPatternTrack>(part.pattern, row, part.track));
            }
        }

        for (const auto& ghost : part.notes)
        {
            domain::Note note{};
            note.id = domain::NoteId::generate();
            note.pitch = ghost.pitch;
            note.velocity = ghost.velocity;
            note.startBeats = ghost.startBeats;
            note.lengthBeats = ghost.lengthBeats;
            out.commands.push_back(std::make_unique<domain::AddNote>(row, note));
        }

        roles += (roles.empty() ? "" : ", ") + std::string{domain::generation::describe(part.role)};
    }

    out.group.label = "génération : " + roles;
    out.group.origin.actor = domain::Actor::generator;
    return out;
}

std::vector<ListeningHost::Line> ZoneProposal::lines() const
{
    std::vector<ListeningHost::Line> out;
    for (const auto& part : parts_)
    {
        ListeningHost::Line line{part.track, part.pattern, part.fromBeats, part.toBeats, part.notes};
        line.songBeats = part.songBeats;
        line.isNew = part.isNew;
        line.lengthBeats = part.lengthBeats;
        out.push_back(std::move(line));
    }
    return out;
}

std::string ZoneProposal::sentence(double beatsPerBar) const
{
    if (parts_.empty())
        return {};

    auto out = domain::generation::lengthWords(zone_.toBeats - zone_.fromBeats, beatsPerBar) + " en " +
               domain::generation::describe(parts_.front().constraints.key.value) + " : ";
    for (std::size_t index = 0; index < parts_.size(); ++index)
    {
        const auto& part = parts_[index];
        if (index > 0)
            out += index + 1 == parts_.size() ? " et " : ", ";
        out += std::string{domain::generation::describe(part.role)};
    }
    return out;
}

} // namespace daw::ui
