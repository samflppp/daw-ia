#pragma once

#include "daw/domain/Ids.h"
#include "daw/domain/Result.h"
#include "daw/domain/command/CommandBus.h"
#include "daw/domain/generation/Constraints.h"
#include "daw/domain/generation/Generator.h"
#include "daw/domain/project/ProjectState.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace daw::ui
{

// Notes the piano roll shows in grey over a range, and nothing else.
//
// A proposal lives here and nowhere in the project: no command, no history
// entry, no identifier drawn, until it is accepted. Rejecting it leaves the
// project, the history and the journal exactly as they were -- the only trace
// it ever leaves is the one group Tab writes.
//
// It holds a copy of what it was made from, never a pointer into the project,
// and compares a hash of that copy after every change of the project. An undo
// of something else that touched the notes around the range regenerates the
// same variant from the new context; a change elsewhere leaves it alone; the
// pattern or the row gone closes it.
class GhostProposal
{
public:
    [[nodiscard]] static domain::Result<GhostProposal> open(const domain::ProjectState& state,
                                                            domain::PatternId pattern,
                                                            domain::TrackId track,
                                                            double fromBeats,
                                                            double toBeats,
                                                            domain::generation::Interpretation interpretation,
                                                            const domain::generation::StyleModel& model);

    // The proposal keeps the model by reference: a temporary would be gone
    // before the first variant is drawn.
    static domain::Result<GhostProposal> open(const domain::ProjectState&,
                                              domain::PatternId,
                                              domain::TrackId,
                                              double,
                                              double,
                                              domain::generation::Interpretation,
                                              const domain::generation::StyleModel&&) = delete;

    // The same, keeping the model alive: a style learned from the projects is
    // built again at each Ctrl+G, and the one a proposal was drawn with must
    // outlive the next one.
    [[nodiscard]] static domain::Result<GhostProposal>
    open(const domain::ProjectState& state,
         domain::PatternId pattern,
         domain::TrackId track,
         double fromBeats,
         double toBeats,
         domain::generation::Interpretation interpretation,
         std::shared_ptr<const domain::generation::StyleModel> model);

    // The variant on screen. Drawn on first use.
    [[nodiscard]] const std::vector<domain::generation::GhostNote>& notes();

    // Alt + wheel: the next or the previous variant. Returns the rank shown,
    // which stays put at either end.
    int shift(int delta);
    [[nodiscard]] int rank() const noexcept { return rank_; }
    [[nodiscard]] int drawn() const noexcept { return variants_->drawn(); }

    enum class Refresh
    {
        unchanged,
        regenerated,
        closed
    };

    // Called after every change of the project.
    [[nodiscard]] Refresh refresh(const domain::ProjectState& state);

    // The commands Tab sends: the notes of the row inside the range removed,
    // the proposed ones added. The row must exist; when it does not, the
    // caller puts the commands that open it first, in the same group.
    struct Acceptance
    {
        std::vector<std::unique_ptr<domain::Command>> commands;
        domain::GroupOptions group;
        std::vector<domain::NoteId> added;
    };
    [[nodiscard]] Acceptance accept(const domain::ProjectState& state, domain::ClipId row);

    [[nodiscard]] domain::PatternId pattern() const noexcept { return pattern_; }
    [[nodiscard]] domain::TrackId track() const noexcept { return track_; }
    [[nodiscard]] double fromBeats() const noexcept { return variants_->context().fromBeats; }
    [[nodiscard]] double toBeats() const noexcept { return variants_->context().toBeats; }
    [[nodiscard]] const domain::generation::ResolvedConstraints& constraints() const noexcept
    {
        return variants_->constraints();
    }
    [[nodiscard]] const domain::generation::Interpretation& interpretation() const noexcept
    {
        return interpretation_;
    }

    // Whether a note of the row is one Tab would replace.
    [[nodiscard]] bool replaces(const domain::Note& note) const noexcept;

    // How long the last draw took, in milliseconds. Logged, and checked by the
    // verification against the 16 ms of T0.
    [[nodiscard]] double lastDrawMs() const noexcept { return lastDrawMs_; }

private:
    GhostProposal(domain::PatternId pattern,
                  domain::TrackId track,
                  domain::generation::Interpretation interpretation,
                  const domain::generation::StyleModel& model,
                  domain::generation::Context context);

    void rebuild(domain::generation::Context context);

    domain::PatternId pattern_;
    domain::TrackId track_;
    double requestedFrom_{0.0};
    double requestedTo_{0.0};
    domain::generation::Interpretation interpretation_;
    const domain::generation::StyleModel* model_;
    std::shared_ptr<const domain::generation::StyleModel> kept_;
    std::unique_ptr<domain::generation::Variants> variants_;
    std::uint64_t contextHash_{0};
    int rank_{0};
    double lastDrawMs_{0.0};
};

} // namespace daw::ui
