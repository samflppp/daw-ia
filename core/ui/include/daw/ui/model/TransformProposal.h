#pragma once

#include "daw/domain/Result.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/generation/StyleModel.h"
#include "daw/domain/generation/Transform.h"
#include "daw/domain/project/ProjectState.h"
#include "daw/ui/model/GhostProposal.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace daw::ui
{

// Notes of the zone reworked (S16), shown in grey until accepted: the
// transformations of generation/Transform.h, held the way GhostProposal holds
// a generation. It sits beside GhostProposal rather than in it: the proposal
// of new notes is the engine of S14-S15 and does not change.
//
// Same rules: nothing in the project until accept(), a copy of what it was
// made from and never a pointer into the project, a hash compared after every
// change. Accepting removes the notes of the zone and writes the reworked
// ones, in one group from the generator.
class TransformProposal
{
public:
    using Refresh = GhostProposal::Refresh;
    using Acceptance = GhostProposal::Acceptance;

    [[nodiscard]] static domain::Result<TransformProposal>
    open(const domain::ProjectState& state,
         domain::PatternId pattern,
         domain::TrackId track,
         double fromBeats,
         double toBeats,
         domain::generation::Interpretation interpretation,
         domain::generation::Transform transform,
         std::shared_ptr<const domain::generation::StyleModel> model);

    // The variant on screen, drawn on first use. Transformations that leave
    // nothing to chance (darker, busier...) have one.
    [[nodiscard]] const std::vector<domain::generation::GhostNote>& notes();
    int shift(int delta);
    [[nodiscard]] int rank() const noexcept { return rank_; }
    [[nodiscard]] int drawn() const noexcept { return static_cast<int>(drawn_.size()); }

    [[nodiscard]] Refresh refresh(const domain::ProjectState& state);
    [[nodiscard]] Acceptance accept(const domain::ProjectState& state, domain::ClipId row);

    [[nodiscard]] const std::vector<domain::generation::GhostNote>& source() const noexcept
    {
        return source_;
    }
    [[nodiscard]] domain::generation::Transform transform() const noexcept { return transform_; }
    [[nodiscard]] domain::PatternId pattern() const noexcept { return pattern_; }
    [[nodiscard]] domain::TrackId track() const noexcept { return track_; }
    [[nodiscard]] double fromBeats() const noexcept { return context_.fromBeats; }
    [[nodiscard]] double toBeats() const noexcept { return context_.toBeats; }
    [[nodiscard]] const domain::generation::ResolvedConstraints& constraints() const noexcept
    {
        return constraints_;
    }
    [[nodiscard]] const domain::generation::Interpretation& interpretation() const noexcept
    {
        return interpretation_;
    }

private:
    TransformProposal() = default;
    void rebuild(domain::generation::Context context);
    void drawUpTo(int rank);

    domain::PatternId pattern_;
    domain::TrackId track_;
    double requestedFrom_{0.0};
    double requestedTo_{0.0};
    domain::generation::Interpretation interpretation_;
    domain::generation::Transform transform_{domain::generation::Transform::keepRhythm};
    std::shared_ptr<const domain::generation::StyleModel> model_;
    domain::generation::Context context_;
    domain::generation::ResolvedConstraints constraints_;
    std::vector<domain::generation::GhostNote> source_;
    std::vector<std::vector<domain::generation::GhostNote>> drawn_;
    int nextSeed_{0};
    std::uint64_t contextHash_{0};
    int rank_{0};
};

} // namespace daw::ui
