#include "daw/ui/model/GhostProposal.h"

#include "daw/domain/commands/AddNote.h"
#include "daw/domain/commands/NoteCommands.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace daw::ui
{

using namespace domain::generation;

domain::Result<GhostProposal> GhostProposal::open(const domain::ProjectState& state,
                                                  domain::PatternId pattern,
                                                  domain::TrackId track,
                                                  double fromBeats,
                                                  double toBeats,
                                                  Interpretation interpretation,
                                                  const StyleModel& model)
{
    auto context = Context::of(state, pattern, track, fromBeats, toBeats);
    if (!context)
        return context.error();

    GhostProposal proposal{pattern, track, std::move(interpretation), model, std::move(context).value()};
    proposal.requestedFrom_ = fromBeats;
    proposal.requestedTo_ = toBeats;
    return proposal;
}

domain::Result<GhostProposal> GhostProposal::open(const domain::ProjectState& state,
                                                  domain::PatternId pattern,
                                                  domain::TrackId track,
                                                  double fromBeats,
                                                  double toBeats,
                                                  Interpretation interpretation,
                                                  std::shared_ptr<const StyleModel> model)
{
    if (model == nullptr)
        return domain::fail(domain::ErrorCode::invalidArgument, "no style model");

    auto opened = open(state, pattern, track, fromBeats, toBeats, std::move(interpretation), *model);
    if (opened)
        opened.value().kept_ = std::move(model);
    return opened;
}

GhostProposal::GhostProposal(domain::PatternId pattern,
                             domain::TrackId track,
                             Interpretation interpretation,
                             const StyleModel& model,
                             Context context)
    : pattern_{pattern}
    , track_{track}
    , interpretation_{std::move(interpretation)}
    , model_{&model}
{
    rebuild(std::move(context));
}

void GhostProposal::rebuild(Context context)
{
    contextHash_ = context.hash();
    const auto resolved = resolve(interpretation_.constraints, context);
    variants_ = std::make_unique<Variants>(std::move(context), resolved, *model_);
}

const std::vector<GhostNote>& GhostProposal::notes()
{
    const auto started = std::chrono::steady_clock::now();
    const auto& shown = variants_->at(rank_);
    lastDrawMs_ =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return shown;
}

int GhostProposal::shift(int delta)
{
    const auto wanted = std::clamp(rank_ + delta, 0, Variants::maxVariants - 1);
    static_cast<void>(variants_->at(wanted));

    // Fewer distinct variants than asked: stay on the last one there is.
    rank_ = std::min(wanted, variants_->drawn() - 1);
    return rank_;
}

GhostProposal::Refresh GhostProposal::refresh(const domain::ProjectState& state)
{
    auto context = Context::of(state, pattern_, track_, requestedFrom_, requestedTo_);
    if (!context)
        return Refresh::closed;

    if (context.value().hash() == contextHash_)
        return Refresh::unchanged;

    rebuild(std::move(context).value());
    return Refresh::regenerated;
}

bool GhostProposal::replaces(const domain::Note& note) const noexcept
{
    constexpr double epsilon = 1e-6;
    return note.startBeats >= fromBeats() - epsilon && note.startBeats < toBeats() - epsilon;
}

GhostProposal::Acceptance GhostProposal::accept(const domain::ProjectState& state, domain::ClipId row)
{
    Acceptance out;

    if (const auto* clip = state.findClip(row); clip != nullptr)
    {
        for (const auto& note : clip->notes)
        {
            if (replaces(note))
                out.commands.push_back(std::make_unique<domain::RemoveNote>(row, note.id));
        }
    }

    // The identifiers are drawn now, and only now.
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

    out.group.label = "génération : " + shortLabel(constraints());
    out.group.origin.actor = domain::Actor::generator;
    return out;
}

} // namespace daw::ui
