#include "daw/ui/model/TransformProposal.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteCommands.h"

#include <algorithm>
#include <utility>

namespace daw::ui
{

using namespace domain::generation;

domain::Result<TransformProposal> TransformProposal::open(const domain::ProjectState& state,
                                                          domain::PatternId pattern,
                                                          domain::TrackId track,
                                                          double fromBeats,
                                                          double toBeats,
                                                          Interpretation interpretation,
                                                          Transform transform,
                                                          std::shared_ptr<const StyleModel> model)
{
    if (model == nullptr)
        return domain::fail(domain::ErrorCode::invalidArgument, "no style model");

    auto context = Context::of(state, pattern, track, fromBeats, toBeats);
    if (!context)
        return context.error();

    TransformProposal out;
    out.pattern_ = pattern;
    out.track_ = track;
    out.requestedFrom_ = fromBeats;
    out.requestedTo_ = toBeats;
    out.interpretation_ = std::move(interpretation);
    out.transform_ = transform;
    out.model_ = std::move(model);
    out.rebuild(std::move(context).value());
    if (out.source_.empty())
        return domain::fail(domain::ErrorCode::invalidArgument, "no note to rework in the zone");
    return out;
}

void TransformProposal::rebuild(Context context)
{
    contextHash_ = context.hash();
    constraints_ = resolve(interpretation_.constraints, context);
    context_ = std::move(context);
    source_ = sourceOf(context_);
    drawn_.clear();
    nextSeed_ = 0;
    rank_ = 0;
}

void TransformProposal::drawUpTo(int rank)
{
    // Distinct variants only, and none equal to the source: a rework that
    // changes nothing is not shown.
    while (static_cast<int>(drawn_.size()) <= rank && nextSeed_ < Variants::maxVariants)
    {
        auto notes =
            domain::generation::transform(transform_, source_, context_, constraints_, *model_, nextSeed_++);
        if (notes.empty() || notes == source_ ||
            std::find(drawn_.begin(), drawn_.end(), notes) != drawn_.end())
            continue;
        drawn_.push_back(std::move(notes));
    }
    if (drawn_.empty())
        drawn_.push_back(source_);
}

const std::vector<GhostNote>& TransformProposal::notes()
{
    drawUpTo(rank_);
    return drawn_[static_cast<std::size_t>(std::min(rank_, drawn() - 1))];
}

int TransformProposal::shift(int delta)
{
    const auto wanted = std::clamp(rank_ + delta, 0, Variants::maxVariants - 1);
    drawUpTo(wanted);
    rank_ = std::min(wanted, drawn() - 1);
    return rank_;
}

TransformProposal::Refresh TransformProposal::refresh(const domain::ProjectState& state)
{
    auto context = Context::of(state, pattern_, track_, requestedFrom_, requestedTo_);
    if (!context)
        return Refresh::closed;
    if (context.value().hash() == contextHash_)
        return Refresh::unchanged;

    rebuild(std::move(context).value());
    return source_.empty() ? Refresh::closed : Refresh::regenerated;
}

TransformProposal::Acceptance TransformProposal::accept(const domain::ProjectState& state, domain::ClipId row)
{
    constexpr double epsilon = 1e-6;
    Acceptance out;

    if (const auto* clip = state.findClip(row); clip != nullptr)
    {
        for (const auto& note : clip->notes)
        {
            if (note.startBeats >= fromBeats() - epsilon && note.startBeats < toBeats() - epsilon)
                out.commands.push_back(std::make_unique<domain::RemoveNote>(row, note.id));
        }
    }

    for (const auto& ghost : notes())
    {
        domain::Note note{};
        note.id = domain::NoteId::generate();
        note.pitch = ghost.pitch;
        note.velocity = ghost.velocity;
        note.startBeats = ghost.startBeats;
        note.lengthBeats = ghost.lengthBeats;
        out.added.push_back(note.id);
        out.commands.push_back(std::make_unique<domain::AddNote>(row, note));
    }

    out.group.label = "retouche : " + std::string{describe(transform_)};
    out.group.origin.actor = domain::Actor::generator;
    return out;
}

} // namespace daw::ui
